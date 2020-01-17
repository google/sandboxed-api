// Copyright 2019 Google LLC
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

// A simple sandbox2 testing tool.
//
// Usage:
// sandbox2tool -v=1 -sandbox2_danger_danger_permit_all -logtostderr -- /bin/ls

#include <sys/resource.h>
#include <sys/stat.h>
#include <syscall.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glog/logging.h>
#include "sandboxed_api/util/flag.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/ipc.h"
#include "sandboxed_api/sandbox2/limits.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/sandbox2/result.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"

ABSL_FLAG(bool, sandbox2tool_keep_env, false,
          "Keep current environment variables");
ABSL_FLAG(bool, sandbox2tool_redirect_fd1, false,
          "Receive sandboxee's STDOUT_FILENO (1) and output it locally");
ABSL_FLAG(bool, sandbox2tool_need_networking, false,
          "If user namespaces are enabled, this option will enable "
          "networking (by disabling the network namespace)");
ABSL_FLAG(bool, sandbox2tool_mount_tmp, false,
          "If user namespaces are enabled, this option will create a tmpfs "
          "mount at /tmp");
ABSL_FLAG(bool, sandbox2tool_resolve_and_add_libraries, false,
          "resolve and mount the required libraries for the sandboxee");
ABSL_FLAG(bool, sandbox2tool_pause_resume, false,
          "Pause the process after 3 seconds, resume after the subsequent "
          "3 seconds, kill it after the final 3 seconds");
ABSL_FLAG(bool, sandbox2tool_pause_kill, false,
          "Pause the process after 3 seconds, then SIGKILL it.");
ABSL_FLAG(bool, sandbox2tool_dump_stack, false,
          "Dump the stack trace one second after the process is running.");
ABSL_FLAG(uint64_t, sandbox2tool_cpu_timeout, 60U,
          "CPU timeout in seconds (if > 0)");
ABSL_FLAG(uint64_t, sandbox2tool_walltime_timeout, 60U,
          "Wall-time timeout in seconds (if >0)");
ABSL_FLAG(uint64_t, sandbox2tool_file_size_creation_limit, 1024U,
          "Maximum size of created files");
ABSL_FLAG(string, sandbox2tool_cwd, "/",
          "If not empty, chdir to the directory before sandboxed");
ABSL_FLAG(string, sandbox2tool_additional_bind_mounts, "",
          "If user namespaces are enabled, this option will add additional "
          "bind mounts. Mounts are separated by comma and can optionally "
          "specify a target using \"=>\" "
          "(e.g. \"/usr,/bin,/lib,/tmp/foo=>/etc/passwd\")");

namespace {

void OutputFD(int fd) {
  for (;;) {
    char buf[4096];
    ssize_t rlen = read(fd, buf, sizeof(buf));
    if (rlen < 1) {
      break;
    }
    LOG(INFO) << "Received from the sandboxee (FD STDOUT_FILENO (1)):"
              << "\n========================================\n"
              << std::string(buf, rlen)
              << "\n========================================\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  if (argc < 2) {
    absl::FPrintF(stderr, "Usage: %s [flags] -- cmd args...", argv[0]);
    return EXIT_FAILURE;
  }

  // Pass everything after '--' to the sandbox.
  std::vector<std::string> args;
  sandbox2::util::CharPtrArrToVecString(&argv[1], &args);

  // Pass the current environ pointer, depending on the flag.
  std::vector<std::string> envp;
  if (absl::GetFlag(FLAGS_sandbox2tool_keep_env)) {
    sandbox2::util::CharPtrArrToVecString(environ, &envp);
  }
  auto executor = absl::make_unique<sandbox2::Executor>(argv[1], args, envp);

  int recv_fd1 = -1;
  if (absl::GetFlag(FLAGS_sandbox2tool_redirect_fd1)) {
    // Make the sandboxed process' fd be available as fd in the current process.
    recv_fd1 = executor->ipc()->ReceiveFd(STDOUT_FILENO);
  }

  executor
      ->limits()
      // Remove restrictions on the size of address-space of sandboxed
      // processes.
      ->set_rlimit_as(RLIM64_INFINITY)
      // Kill sandboxed processes with a signal (SIGXFSZ) if it writes more than
      // this to the file-system.
      .set_rlimit_fsize(
          absl::GetFlag(FLAGS_sandbox2tool_file_size_creation_limit))
      // An arbitrary, but empirically safe value.
      .set_rlimit_nofile(1024U)
      .set_walltime_limit(
          absl::Seconds(absl::GetFlag(FLAGS_sandbox2tool_walltime_timeout)));

  if (absl::GetFlag(FLAGS_sandbox2tool_cpu_timeout) > 0) {
    executor->limits()->set_rlimit_cpu(
        absl::GetFlag(FLAGS_sandbox2tool_cpu_timeout));
  }

  sandbox2::PolicyBuilder builder;
  builder.AddPolicyOnSyscall(__NR_tee, {KILL});
  builder.DangerDefaultAllowAll();

  if (absl::GetFlag(FLAGS_sandbox2tool_need_networking)) {
    builder.AllowUnrestrictedNetworking();
  }
  if (absl::GetFlag(FLAGS_sandbox2tool_mount_tmp)) {
    builder.AddTmpfs("/tmp");
  }

  auto mounts_string = absl::GetFlag(FLAGS_sandbox2tool_additional_bind_mounts);
  if (!mounts_string.empty()) {
    for (auto mount : absl::StrSplit(mounts_string, ',')) {
      std::vector<std::string> source_target = absl::StrSplit(mount, "=>");
      auto source = source_target[0];
      auto target = source_target[0];
      if (source_target.size() == 2) {
        target = source_target[1];
      }
      struct stat64 st;
      PCHECK(stat64(source.c_str(), &st) != -1)
          << "could not stat additional mount " << source;
      if ((st.st_mode & S_IFMT) == S_IFDIR) {
        builder.AddDirectoryAt(source, target, true);
      } else {
        builder.AddFileAt(source, target, true);
      }
    }
  }

  if (absl::GetFlag(FLAGS_sandbox2tool_resolve_and_add_libraries)) {
    builder.AddLibrariesForBinary(argv[1]);
  }

  auto policy = builder.BuildOrDie();

  // Current working directory.
  if (!absl::GetFlag(FLAGS_sandbox2tool_cwd).empty()) {
    executor->set_cwd(absl::GetFlag(FLAGS_sandbox2tool_cwd));
  }

  // Instantiate the Sandbox2 object with policies and executors.
  sandbox2::Sandbox2 s2(std::move(executor), std::move(policy));

  // This sandbox runs asynchronously. If there was no OutputFD() loop receiving
  // the data from the recv_fd1, one could just use Sandbox2::Run().
  if (s2.RunAsync()) {
    if (absl::GetFlag(FLAGS_sandbox2tool_pause_resume)) {
      sleep(3);
      kill(s2.GetPid(), SIGSTOP);
      sleep(3);
      s2.SetWallTimeLimit(3);
      kill(s2.GetPid(), SIGCONT);
    } else if (absl::GetFlag(FLAGS_sandbox2tool_pause_kill)) {
      sleep(3);
      kill(s2.GetPid(), SIGSTOP);
      sleep(1);
      kill(s2.GetPid(), SIGKILL);
      sleep(1);
    } else if (absl::GetFlag(FLAGS_sandbox2tool_dump_stack)) {
      sleep(1);
      s2.DumpStackTrace();
    } else if (absl::GetFlag(FLAGS_sandbox2tool_redirect_fd1)) {
      OutputFD(recv_fd1);
      // We couldn't receive more data from the sandboxee's STDOUT_FILENO, but
      // the process could still be running. Kill it unconditionally. A correct
      // final status code will be reported instead of Result::EXTERNAL_KILL.
      s2.Kill();
    }
  } else {
    LOG(ERROR) << "Sandbox failed";
  }

  auto result = s2.AwaitResult();

  if (result.final_status() != sandbox2::Result::OK) {
    LOG(ERROR) << "Sandbox error: " << result.ToString();
    return 2;  // sandbox violation
  }
  auto code = result.reason_code();
  if (code) {
    LOG(ERROR) << "Child exited with non-zero " << code;
    return 1;  // normal child error
  }

  return EXIT_SUCCESS;
}
