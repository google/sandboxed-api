// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Implementation of the sandbox2::StackTrace class.

#include "sandboxed_api/sandbox2/stack_trace.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <syscall.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/time.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/mounts.h"
#include "sandboxed_api/sandbox2/namespace.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/unwind/unwind.h"
#include "sandboxed_api/sandbox2/unwind/unwind.pb.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status_macros.h"

ABSL_FLAG(bool, sandbox_disable_all_stack_traces, false,
          "Completely disable stack trace collection for sandboxees");

ABSL_FLAG(bool, sandbox_libunwind_crash_handler, true,
          "Sandbox libunwind when handling violations (preferred)");

namespace sandbox2 {
namespace {

namespace file = ::sapi::file;
namespace file_util = ::sapi::file_util;

// Similar to GetStackTrace() but without using the sandbox to isolate
// libunwind.
absl::StatusOr<std::vector<std::string>> UnsafeGetStackTrace(pid_t pid) {
  LOG(WARNING) << "Using non-sandboxed libunwind";
  return RunLibUnwindAndSymbolizer(pid, kDefaultMaxFrames);
}

bool IsSameFile(const std::string& path, const std::string& other) {
  struct stat buf, other_buf;
  if (stat(path.c_str(), &buf) != 0 || stat(other.c_str(), &other_buf) != 0) {
    return false;
  }
  return buf.st_dev == other_buf.st_dev && buf.st_ino == other_buf.st_ino &&
         buf.st_mode == other_buf.st_mode &&
         buf.st_nlink == other_buf.st_nlink && buf.st_uid == other_buf.st_uid &&
         buf.st_gid == other_buf.st_gid && buf.st_rdev == other_buf.st_rdev &&
         buf.st_size == other_buf.st_size &&
         buf.st_blksize == other_buf.st_blksize &&
         buf.st_blocks == other_buf.st_blocks;
}

}  // namespace

class StackTracePeer {
 public:
  static absl::StatusOr<std::unique_ptr<Policy>> GetPolicy(
      pid_t target_pid, const std::string& maps_file,
      const std::string& app_path, const std::string& exe_path,
      const Namespace* ns, bool uses_custom_forkserver);

  static absl::StatusOr<std::vector<std::string>> LaunchLibunwindSandbox(
      const Regs* regs, const Namespace* ns, bool uses_custom_forkserver,
      int recursion_depth);
};

absl::StatusOr<std::unique_ptr<Policy>> StackTracePeer::GetPolicy(
    pid_t target_pid, const std::string& maps_file, const std::string& app_path,
    const std::string& exe_path, const Namespace* ns,
    bool uses_custom_forkserver) {
  PolicyBuilder builder;
  if (uses_custom_forkserver) {
    // Custom forkserver just forks, the binary is loaded outside of the
    // sandboxee's mount namespace.
    // Add all possible libraries without the need of parsing the binary
    // or /proc/pid/maps.
    for (const auto& library_path : {
             "/usr/lib64",
             "/usr/lib",
             "/lib64",
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
  } else {
    // Use the mounttree of the original executable.
    CHECK(ns != nullptr);
    Mounts mounts = ns->mounts();
    mounts.Remove("/proc").IgnoreError();
    mounts.Remove(app_path).IgnoreError();
    builder.SetMounts(std::move(mounts));
  }
  builder.AllowOpen()
      .AllowRead()
      .AllowWrite()
      .AllowSyscall(__NR_close)
      .AllowExit()
      .AllowHandleSignals()
      .AllowTcMalloc()
      .AllowSystemMalloc()
      // for Comms:RecvFD
      .AllowSyscall(__NR_recvmsg)

      // libunwind
      .AllowMmapWithoutExec()
      .AllowStat()
      .AllowSyscall(__NR_lseek)
#ifdef __NR__llseek
      .AllowSyscall(__NR__llseek)  // Newer glibc on PPC
#endif
      .AllowSyscall(__NR_mincore)
      .AllowSyscall(__NR_munmap)
      .AllowPipe()

      // Symbolizer
      .AllowSyscall(__NR_brk)
      .AllowTime()

      // Other
      .AllowDup()
      .AllowSafeFcntl()
      .AllowGetPIDs()

      // Required for our ptrace replacement.
      .TrapPtrace()

      // Add proc maps.
      .AddFileAt(maps_file,
                 file::JoinPath("/proc", absl::StrCat(target_pid), "maps"))
      .AddFileAt(maps_file,
                 file::JoinPath("/proc", absl::StrCat(target_pid), "task",
                                absl::StrCat(target_pid), "maps"))

      // Add the binary itself.
      .AddFileAt(exe_path, app_path)
      .AllowLlvmCoverage();

  return builder.TryBuild();
}

namespace internal {
SandboxPeer::SpawnFn SandboxPeer::spawn_fn_ = nullptr;
}  // namespace internal

absl::StatusOr<std::vector<std::string>> StackTracePeer::LaunchLibunwindSandbox(
    const Regs* regs, const Namespace* ns, bool uses_custom_forkserver,
    int recursion_depth) {
  const pid_t pid = regs->pid();

  sapi::file_util::fileops::FDCloser memory_fd(
      open(absl::StrCat("/proc/", pid, "/mem").c_str(), O_RDONLY));
  if (memory_fd.get() == -1) {
    return absl::InternalError("Opening sandboxee process memory failed");
  }
  // Tell executor to use this special internal mode. Using `new` to access a
  // non-public constructor.
  auto executor = absl::WrapUnique(new Executor(pid, recursion_depth));

  executor->limits()->set_rlimit_cpu(10).set_walltime_limit(absl::Seconds(5));

  // Temporary directory used to provide files from /proc to the unwind sandbox.
  char unwind_temp_directory_template[] = "/tmp/.sandbox2_unwind_XXXXXX";
  char* unwind_temp_directory = mkdtemp(unwind_temp_directory_template);
  if (!unwind_temp_directory) {
    return absl::InternalError(
        "Could not create temporary directory for unwinding");
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
    return absl::InternalError("Could not copy maps file");
  }

  // Get path to the binary.
  // app_path contains the path like it is also in /proc/pid/maps. It is
  // relative to the sandboxee's mount namespace. If it is not existing
  // (anymore) it will have a ' (deleted)' suffix.
  std::string app_path;
  std::string proc_pid_exe = file::JoinPath("/proc", absl::StrCat(pid), "exe");
  if (!file_util::fileops::ReadLinkAbsolute(proc_pid_exe, &app_path)) {
    return absl::InternalError("Could not obtain absolute path to the binary");
  }

  std::string exe_path;
  if (IsSameFile(app_path, proc_pid_exe)) {
    exe_path = app_path;
  } else {
    // The exe_path will have a mountable path of the application, even if it
    // was removed. Resolve app_path backing file.
    exe_path = ns ? ns->mounts().ResolvePath(app_path).value_or("") : "";
  }

  if (exe_path.empty()) {
    // File was probably removed.
    LOG(WARNING) << "File was removed, using /proc/pid/exe.";
    app_path = std::string(absl::StripSuffix(app_path, " (deleted)"));
    // Create a copy of /proc/pid/exe, mount that one.
    exe_path = file::JoinPath(unwind_temp_directory, "exe");
    if (!file_util::fileops::CopyFile(proc_pid_exe, exe_path, 0700)) {
      return absl::InternalError("Could not copy /proc/pid/exe");
    }
  }

  VLOG(1) << "Resolved binary: " << app_path << " / " << exe_path;

  // Add mappings for the binary (as they might not have been added due to the
  // forkserver).
  SAPI_ASSIGN_OR_RETURN(
      std::unique_ptr<Policy> policy,
      StackTracePeer::GetPolicy(pid, unwind_temp_maps_path, app_path, exe_path,
                                ns, uses_custom_forkserver));

  VLOG(1) << "Running libunwind sandbox";
  auto sandbox =
      internal::SandboxPeer::Spawn(std::move(executor), std::move(policy));
  Comms* comms = sandbox->comms();

  UnwindSetup msg;
  msg.set_pid(pid);
  msg.set_regs(reinterpret_cast<const char*>(&regs->user_regs_),
               sizeof(regs->user_regs_));
  msg.set_default_max_frames(kDefaultMaxFrames);

  absl::Cleanup kill_sandbox = [&sandbox]() {
    sandbox->Kill();
    sandbox2::Result result = sandbox->AwaitResult();
    LOG(INFO) << "Libunwind execution status: " << result.ToString();
  };

  if (!comms->SendProtoBuf(msg)) {
    return absl::InternalError("Sending libunwind setup message failed");
  }
  if (!comms->SendFD(memory_fd.get())) {
    return absl::InternalError("Sending sandboxee's memory fd failed");
  }
  absl::Status status;
  if (!comms->RecvStatus(&status)) {
    return absl::InternalError(
        "Receiving status from libunwind sandbox failed");
  }
  SAPI_RETURN_IF_ERROR(status);

  UnwindResult result;
  if (!comms->RecvProtoBuf(&result)) {
    return absl::InternalError("Receiving libunwind result failed");
  }

  std::move(kill_sandbox).Cancel();

  auto sandbox_result = sandbox->AwaitResult();

  LOG(INFO) << "Libunwind execution status: " << sandbox_result.ToString();

  if (sandbox_result.final_status() != Result::OK) {
    return absl::InternalError(
        absl::StrCat("libunwind sandbox did not finish properly: ",
                     sandbox_result.ToString()));
  }

  return std::vector<std::string>(result.stacktrace().begin(),
                                  result.stacktrace().end());
}

absl::StatusOr<std::vector<std::string>> GetStackTrace(
    const Regs* regs, const Namespace* ns, bool uses_custom_forkserver,
    int recursion_depth) {
  if (absl::GetFlag(FLAGS_sandbox_disable_all_stack_traces)) {
    return absl::UnavailableError("Stacktraces disabled");
  }
  if (!regs) {
    return absl::InvalidArgumentError(
        "Could not obtain stacktrace, regs == nullptr");
  }

  if (!absl::GetFlag(FLAGS_sandbox_libunwind_crash_handler)) {
    return UnsafeGetStackTrace(regs->pid());
  }

  // Show a warning if sandboxed libunwind is requested but we're running in
  // a sanitizer build (= we can't use sandboxed libunwind).
  if (sapi::sanitizers::IsAny()) {
    LOG(WARNING) << "Sanitizer build, using non-sandboxed libunwind";
    return UnsafeGetStackTrace(regs->pid());
  }

  return StackTracePeer::LaunchLibunwindSandbox(
      regs, ns, uses_custom_forkserver, recursion_depth);
}

std::vector<std::string> CompactStackTrace(
    const std::vector<std::string>& stack_trace) {
  std::vector<std::string> compact_trace;
  compact_trace.reserve(stack_trace.size() / 2);
  const std::string* prev = nullptr;
  int seen = 0;
  auto add_repeats = [&compact_trace](int seen) {
    if (seen != 0) {
      compact_trace.push_back(
          absl::StrCat("(previous frame repeated ", seen, " times)"));
    }
  };
  for (const auto& frame : stack_trace) {
    if (prev && frame == *prev) {
      ++seen;
    } else {
      prev = &frame;
      add_repeats(seen);
      seen = 0;
      compact_trace.push_back(frame);
    }
  }
  add_repeats(seen);
  return compact_trace;
}

}  // namespace sandbox2
