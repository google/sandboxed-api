// Copyright 2020 Google LLC
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

#ifndef SAPI_LIBARCHIVE_EXAMPLES_SANDBOX_H
#define SAPI_LIBARCHIVE_EXAMPLES_SANDBOX_H

#include <asm/unistd_64.h>
#include <linux/fs.h>

#include "libarchive_sapi.sapi.h"  // NOLINT(build/include)
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/fileops.h"

// When creating an archive, we need read permissions on each of the
// file/directory added in the archive. Also, in order to create the archive, we
// map "/output" with the basename of the archive. This way, the program can
// create the file without having access to anything else.
class SapiLibarchiveSandboxCreate : public LibarchiveSandbox {
 public:
  SapiLibarchiveSandboxCreate(const std::vector<std::string>& files,
                              absl::string_view archive_path)
      : files_(files), archive_path_(archive_path) {}

 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    sandbox2::PolicyBuilder policy =
        sandbox2::PolicyBuilder()
            .AddDirectoryAt(archive_path_, "/output", false)
            .AllowRead()
            .AllowWrite()
            .AllowOpen()
            .AllowSystemMalloc()
            .AllowGetIDs()
            .AllowSafeFcntl()
            .AllowStat()
            .AllowExit()
            .AllowSyscalls({
                __NR_futex,
                __NR_lseek,
                __NR_close,
                __NR_gettid,
                __NR_umask,
                __NR_utimensat,
                __NR_unlink,
                __NR_mkdir,
                __NR_fstatfs,
                __NR_socket,
                __NR_connect,
                __NR_flistxattr,
                __NR_recvmsg,
                __NR_getdents64,
            })
            // Allow ioctl only for FS_IOC_GETFLAGS.
            .AddPolicyOnSyscall(__NR_ioctl,
                                {ARG(1), JEQ(FS_IOC_GETFLAGS, ALLOW)});

    // We check whether the entry is a file or a directory.
    for (const auto& i : files_) {
      struct stat s;
      CHECK(stat(i.c_str(), &s) == 0) << "Could not stat " << i;
      if (S_ISDIR(s.st_mode)) {
        policy = policy.AddDirectory(i);
      } else {
        policy = policy.AddFile(i);
      }
    }

    return policy.BuildOrDie();
  }

  const std::vector<std::string> files_;
  absl::string_view archive_path_;
};

// When an archive is extracted, the generated files/directories will be placed
// relative to the current working directory. In order to add permissions to
// this we create a temporary directory at every extraction. Then, we change the
// directory of the sandboxed process to that directory and map it to the
// current "real" working directory. This way the contents of the archived will
// pe placed correctly without offering additional permission.
class SapiLibarchiveSandboxExtract : public LibarchiveSandbox {
 public:
  SapiLibarchiveSandboxExtract(absl::string_view archive_path, int do_extract,
                               absl::string_view tmp_dir)
      : archive_path_(archive_path),
        do_extract_(do_extract),
        tmp_dir_(tmp_dir) {}

 private:
  void ModifyExecutor(sandbox2::Executor* executor) override {
    // If the user only wants to list the entries in the archive, we do
    // not need to worry about changing directories;
    if (do_extract_) {
      executor->set_cwd(std::string(tmp_dir_));
    }
  }

  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    sandbox2::PolicyBuilder policy = sandbox2::PolicyBuilder()
                                         .AllowRead()
                                         .AllowWrite()
                                         .AllowOpen()
                                         .AllowSystemMalloc()
                                         .AllowGetIDs()
                                         .AllowSafeFcntl()
                                         .AllowStat()
                                         .AllowExit()
                                         .AllowSyscalls({
                                             __NR_futex,
                                             __NR_lseek,
                                             __NR_close,
                                             __NR_gettid,
                                             __NR_umask,
                                             __NR_utimensat,
                                             __NR_unlink,
                                             __NR_mkdir,
                                         })
                                         .AddFile(archive_path_);

    if (do_extract_) {
      // Get the real cwd and map it to the temporary directory in which
      // the sandboxed process takes place().
      std::string cwd = sandbox2::file_util::fileops::GetCWD();
      policy = policy.AddDirectoryAt(cwd, tmp_dir_, false);
    }
    return policy.BuildOrDie();
  }

  absl::string_view archive_path_;
  absl::string_view tmp_dir_;
  const int do_extract_;
};

#endif  // SAPI_LIBARCHIVE_EXAMPLES_SANDBOX_H
