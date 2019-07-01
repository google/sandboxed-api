// Copyright 2019 Google LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Implementation of the sandbox2::StackTrace class.

#include "sandboxed_api/sandbox2/stack-trace.h"

#include <sys/capability.h>
#include <sys/resource.h>
#include <syscall.h>
#include <memory>
#include <utility>
#include <vector>

#include <glog/logging.h>
#include "sandboxed_api/util/flag.h"
#include "absl/memory/memory.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "libcap/include/sys/capability.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/unwind/unwind.h"
#include "sandboxed_api/sandbox2/unwind/unwind.pb.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/path.h"

ABSL_FLAG(bool, sandbox_disable_all_stack_traces, false,
          "Completely disable stack trace collection for sandboxees");

ABSL_FLAG(bool, sandbox_libunwind_crash_handler, true,
          "Sandbox libunwind when handling violations (preferred)");

namespace sandbox2 {

class StackTracePeer {
 public:
  static std::unique_ptr<Policy> GetPolicy(pid_t target_pid,
                                           const std::string& maps_file,
                                           const std::string& app_path,
                                           const std::string& exe_path,
                                           const Mounts& mounts);

  static bool LaunchLibunwindSandbox(const Regs* regs, const Mounts& mounts,
                                     UnwindResult* result,
                                     const std::string& delim);
};

std::unique_ptr<Policy> StackTracePeer::GetPolicy(pid_t target_pid,
                                                  const std::string& maps_file,
                                                  const std::string& app_path,
                                                  const std::string& exe_path,
                                                  const Mounts& mounts) {
  PolicyBuilder builder;
  builder
      // Use the mounttree of the original executable as starting point.
      .SetMounts(mounts)
      .AllowOpen()
      .AllowRead()
      .AllowWrite()
      .AllowSyscall(__NR_close)
      .AllowMmap()
      .AllowExit()
      .AllowHandleSignals()

      // libunwind
      .AllowSyscall(__NR_fstat)
      .AllowSyscall(__NR_lseek)
      .AllowSyscall(__NR_mincore)
      .AllowSyscall(__NR_mprotect)
      .AllowSyscall(__NR_munmap)
      .AllowSyscall(__NR_pipe2)

      // Symbolizer
      .AllowSyscall(__NR_brk)
      .AllowSyscall(__NR_clock_gettime)

      // Other
      .AllowSyscall(__NR_dup)
      .AllowSyscall(__NR_fcntl)
      .AllowSyscall(__NR_getpid)
      .AllowSyscall(__NR_gettid)
      .AllowSyscall(__NR_madvise)

      // Required for our ptrace replacement.
      .AddPolicyOnSyscall(
          __NR_process_vm_readv,
          {
              // The pid technically is a 64bit int, however
              // Linux usually uses max 16 bit, so we are fine
              // with comparing only 32 bits here.
              ARG_32(0),
              JEQ32(static_cast<unsigned int>(target_pid), ALLOW),
              JEQ32(static_cast<unsigned int>(1), ALLOW),
          })

      .EnableNamespaces()

      // Add proc maps.
      .AddFileAt(maps_file,
                 file::JoinPath("/proc", absl::StrCat(target_pid), "maps"))
      .AddFileAt(maps_file,
                 file::JoinPath("/proc", absl::StrCat(target_pid), "task",
                                absl::StrCat(target_pid), "maps"))

      // Add the binary itself.
      .AddFileAt(exe_path, app_path);

  // Add all possible libraries without the need of parsing the binary
  // or /proc/pid/maps.
  for (const auto& library_path : {
           "/usr/lib",
           "/lib",
       }) {
    if (access(library_path, F_OK) != -1) {
      VLOG(1) << "Adding library folder '" << library_path << "'";
      builder.AddDirectory(library_path);
    } else {
      VLOG(1) << "Could not add library folder '" << library_path
              << "' as it does not exist";
    }
  }

  auto policy_or = builder.TryBuild();
  if (!policy_or.ok()) {
    LOG(ERROR) << "Creating stack unwinder sandbox policy failed";
    return nullptr;
  }
  std::unique_ptr<Policy> policy = std::move(policy_or).ValueOrDie();
  auto keep_capabilities = absl::make_unique<std::vector<cap_value_t>>();
  keep_capabilities->push_back(CAP_SYS_PTRACE);
  policy->AllowUnsafeKeepCapabilities(std::move(keep_capabilities));
  // Use no special namespace flags when cloning. We will join an existing
  // user namespace and will unshare() afterwards (See forkserver.cc).
  policy->GetNamespace()->clone_flags_ = 0;
  return policy;
}

bool StackTracePeer::LaunchLibunwindSandbox(const Regs* regs,
                                            const Mounts& mounts,
                                            sandbox2::UnwindResult* result,
                                            const std::string& delim) {
  const pid_t pid = regs->pid();

  // Tell executor to use this special internal mode.
  std::vector<std::string> argv;
  std::vector<std::string> envp;

  // We're not using absl::make_unique here as we're a friend of this specific
  // constructor and using make_unique won't work.
  auto executor = absl::WrapUnique(new Executor(pid));

  executor->limits()
      ->set_rlimit_as(RLIM64_INFINITY)
      .set_rlimit_cpu(10)
      .set_walltime_limit(absl::Seconds(5));

  // Temporary directory used to provide files from /proc to the unwind sandbox.
  char unwind_temp_directory_template[] = "/tmp/.sandbox2_unwind_XXXXXX";
  char* unwind_temp_directory = mkdtemp(unwind_temp_directory_template);
  if (!unwind_temp_directory) {
    LOG(WARNING) << "Could not create temporary directory for unwinding";
    return false;
  }
  struct UnwindTempDirectoryCleanup {
    ~UnwindTempDirectoryCleanup() {
      file_util::fileops::DeleteRecursively(capture);
    }
    char* capture;
  } cleanup{unwind_temp_directory};

  // Copy over important files from the /proc directory as we can't mount them.
  const std::string unwind_temp_maps_path =
      file::JoinPath(unwind_temp_directory, "maps");

  if (!file_util::fileops::CopyFile(
          file::JoinPath("/proc", absl::StrCat(pid), "maps"),
          unwind_temp_maps_path, 0400)) {
    LOG(WARNING) << "Could not copy maps file";
    return false;
  }

  // Get path to the binary.
  // app_path contains the path like it is also in /proc/pid/maps. This is
  // important when the file was removed, it will have a ' (deleted)' suffix.
  std::string app_path;
  // The exe_path will have a mountable path of the application, even if it was
  // removed.
  std::string exe_path;
  std::string proc_pid_exe = file::JoinPath("/proc", absl::StrCat(pid), "exe");
  if (!file_util::fileops::ReadLinkAbsolute(proc_pid_exe, &app_path)) {
    LOG(WARNING) << "Could not obtain absolute path to the binary";
    return false;
  }

  // Check whether the file still exists or not (SAPI).
  if (access(app_path.c_str(), F_OK) == -1) {
    LOG(WARNING) << "File was removed, using /proc/pid/exe.";
    app_path = std::string(absl::StripSuffix(app_path, " (deleted)"));
    // Create a copy of /proc/pid/exe, mount that one.
    exe_path = file::JoinPath(unwind_temp_directory, "exe");
    if (!file_util::fileops::CopyFile(proc_pid_exe, exe_path, 0700)) {
      LOG(WARNING) << "Could not copy /proc/pid/exe";
      return false;
    }
  } else {
    exe_path = app_path;
  }

  VLOG(1) << "Resolved binary: " << app_path << " / " << exe_path;

  // Add mappings for the binary (as they might not have been added due to the
  // forkserver).
  auto policy = StackTracePeer::GetPolicy(pid, unwind_temp_maps_path, app_path,
                                          exe_path, mounts);
  if (!policy) {
    return false;
  }
  auto comms = executor->ipc()->comms();
  Sandbox2 s2(std::move(executor), std::move(policy));

  VLOG(1) << "Running libunwind sandbox";
  s2.RunAsync();
  UnwindSetup msg;
  msg.set_pid(pid);
  msg.set_regs(reinterpret_cast<const char*>(&regs->user_regs_),
               sizeof(regs->user_regs_));
  msg.set_default_max_frames(kDefaultMaxFrames);
  msg.set_delim(delim.c_str(), delim.size());

  bool success = true;
  if (!comms->SendProtoBuf(msg)) {
    LOG(ERROR) << "Sending libunwind setup message failed";
    success = false;
  }

  if (success && !comms->RecvProtoBuf(result)) {
    LOG(ERROR) << "Receiving libunwind result failed";
    success = false;
  }

  if (!success) {
    s2.Kill();
  }
  auto s2_result = s2.AwaitResult();

  LOG(INFO) << "Libunwind execution status: " << s2_result.ToString();

  return success && s2_result.final_status() == Result::OK;
}

std::string GetStackTrace(const Regs* regs, const Mounts& mounts,
                          const std::string& delim) {
  if (absl::GetFlag(FLAGS_sandbox_disable_all_stack_traces)) {
    return "";
  }
  if (!regs) {
    LOG(WARNING) << "Could not obtain stacktrace, regs == nullptr";
    return "[ERROR (noregs)]";
  }

#if defined(THREAD_SANITIZER) || defined(ADDRESS_SANITIZER) || \
    defined(MEMORY_SANITIZER)
  constexpr bool kSanitizerEnabled = true;
#else
  constexpr bool kSanitizerEnabled = false;
#endif

  const bool coverage_enabled =
      getenv("COVERAGE");

  // Show a warning if sandboxed libunwind is requested but we're running in
  // an ASAN / coverage build (= we can't use sandboxed libunwind).
  if (absl::GetFlag(FLAGS_sandbox_libunwind_crash_handler) &&
      (kSanitizerEnabled || coverage_enabled)) {
    LOG_IF(WARNING, kSanitizerEnabled)
        << "Sanitizer build, using non-sandboxed libunwind";
    LOG_IF(WARNING, coverage_enabled)
        << "Coverage build, using non-sandboxed libunwind";
    return UnsafeGetStackTrace(regs->pid(), delim);
  }

  if (!absl::GetFlag(FLAGS_sandbox_libunwind_crash_handler)) {
    return UnsafeGetStackTrace(regs->pid(), delim);
  }
  UnwindResult res;

  if (!StackTracePeer::LaunchLibunwindSandbox(regs, mounts, &res, delim)) {
    return "";
  }
  return res.stacktrace();
}

std::string UnsafeGetStackTrace(pid_t pid, const std::string& delim) {
  LOG(WARNING) << "Using non-sandboxed libunwind";
  std::string stack_trace;
  std::vector<uintptr_t> ips;
  RunLibUnwindAndSymbolizer(pid, &stack_trace, &ips, kDefaultMaxFrames, delim);
  return stack_trace;
}

}  // namespace sandbox2
