#ifndef SAPI_LIBARCHIVE_SANDBOX_H
#define SAPI_LIBARCHIVE_SANDBOX_H

#include <asm/unistd_64.h>
#include <syscall.h>

#include "helpers.h"
#include "libarchive_sapi.sapi.h"

class SapiLibarchiveSandboxCreate : public LibarchiveSandbox {
 public:
  // TODO
  explicit SapiLibarchiveSandboxCreate(const std::vector<std::string> &files,
                                       absl::string_view archive_path)
      : files_(files), archive_path_(archive_path) {}

 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder *) override {
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
                __NR_ioctl,
                __NR_flistxattr,
                __NR_recvmsg,
                __NR_getdents64,
            });

    for (const auto &i : files_) {
        std::cout << "ADD FILE -------" << i << std::endl;
        struct stat s;
        stat(i.c_str(), &s);
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

class SapiLibarchiveSandboxExtract : public LibarchiveSandbox {
 public:
  // TODO
  explicit SapiLibarchiveSandboxExtract(absl::string_view archive_path,
                                        const int do_extract,
                                        absl::string_view tmp_dir)
      : archive_path_(archive_path),
        do_extract_(do_extract),
        tmp_dir_(tmp_dir) {}

 private:
  virtual void ModifyExecutor(sandbox2::Executor *executor) override {
    if (do_extract_) {
      executor = &executor->set_cwd(std::string(tmp_dir_));
    }
  }

  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder *) override {
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
      // map "/output/" to cwd
      std::string cwd = sandbox2::file_util::fileops::GetCWD();
      policy = policy.AddDirectoryAt(cwd, tmp_dir_, false);
    }
    return policy.BuildOrDie();
  }

  absl::string_view archive_path_;
  absl::string_view tmp_dir_;
  const int do_extract_;
};

#endif  // SAPI_LIBARCHIVE_SANDBOX_H