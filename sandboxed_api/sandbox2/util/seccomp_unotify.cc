// Copyright 2025 Google LLC
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

#include "sandboxed_api/sandbox2/util/seccomp_unotify.h"

#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <syscall.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/strerror.h"
#include "sandboxed_api/util/thread.h"

#ifndef SECCOMP_USER_NOTIF_FLAG_CONTINUE
#define SECCOMP_USER_NOTIF_FLAG_CONTINUE 1
#endif

#ifndef SECCOMP_FILTER_FLAG_NEW_LISTENER
#define SECCOMP_FILTER_FLAG_NEW_LISTENER (1UL << 3)
#endif

#ifndef SECCOMP_GET_NOTIF_SIZES
#define SECCOMP_GET_NOTIF_SIZES 3
#endif

#ifndef SECCOMP_IOCTL_NOTIF_RECV
#ifndef SECCOMP_IOWR
#define SECCOMP_IOC_MAGIC '!'
#define SECCOMP_IO(nr) _IO(SECCOMP_IOC_MAGIC, nr)
#define SECCOMP_IOWR(nr, type) _IOWR(SECCOMP_IOC_MAGIC, nr, type)
#endif

// Flags for seccomp notification fd ioctl.
#define SECCOMP_IOCTL_NOTIF_RECV SECCOMP_IOWR(0, struct seccomp_notif)
#define SECCOMP_IOCTL_NOTIF_SEND SECCOMP_IOWR(1, struct seccomp_notif_resp)
#endif

namespace sandbox2 {
namespace util {
namespace {
using ::sapi::file_util::fileops::FDCloser;

int seccomp(unsigned int operation, unsigned int flags, void* args) {
  return Syscall(SYS_seccomp, operation, flags,
                 reinterpret_cast<uintptr_t>(args));
}

class OsSeccompUnotify : public SeccompUnotify::SeccompUnotifyInterface {
 public:
  int GetSizes(seccomp_notif_sizes* sizes) override {
    return seccomp(SECCOMP_GET_NOTIF_SIZES, 0, sizes);
  }
  int ReceiveNotification(int fd, seccomp_notif* req) override {
    return ioctl(fd, SECCOMP_IOCTL_NOTIF_RECV,
                 reinterpret_cast<uintptr_t>(req));
  }
  int SendResponse(int fd, const seccomp_notif_resp& resp) override {
    return ioctl(fd, SECCOMP_IOCTL_NOTIF_SEND,
                 reinterpret_cast<uintptr_t>(&resp));
  }
};

bool TestUserNotifFlagContinueSupport() {
  constexpr int kSpecialSyscall = 0x12345;
  std::array<sock_filter, 4> code = {{
      LOAD_SYSCALL_NR,
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, kSpecialSyscall, 0, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_USER_NOTIF),
      ALLOW,
  }};
  sock_fprog prog = {
      .len = code.size(),
      .filter = code.data(),
  };
  absl::Notification setup_done;
  FDCloser notify_fd;
  sapi::Thread th([&notify_fd, &setup_done, &prog]() {
    absl::Cleanup cleanup = [&setup_done] { setup_done.Notify(); };
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
      VLOG(3) << "Failed to set PR_SET_NO_NEW_PRIVS" << sapi::StrError(errno);
      return;
    }
    int ret = syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER,
                      SECCOMP_FILTER_FLAG_NEW_LISTENER,
                      reinterpret_cast<uintptr_t>(&prog));
    if (ret == -1) {
      VLOG(3) << "Failed seccomp" << sapi::StrError(errno);
      return;
    }
    notify_fd = FDCloser(ret);
    std::move(cleanup).Invoke();
    util::Syscall(kSpecialSyscall);
  });
  absl::Cleanup join_thread = [&th] { th.Join(); };
  setup_done.WaitForNotification();
  if (notify_fd.get() == -1) {
    VLOG(3) << "Failed to setup notify_fd";
    return false;
  }
  SeccompUnotify unotify;
  if (absl::Status status = unotify.Init(std::move(notify_fd)); !status.ok()) {
    VLOG(3) << "Failed to init unotify: " << status;
    return false;
  }
  absl::StatusOr<seccomp_notif> req = unotify.Receive();
  if (!req.ok()) {
    VLOG(3) << "Failed to receive unotify: " << req.status();
    return false;
  }

  if (absl::Status status = unotify.RespondContinue(*req); !status.ok()) {
    VLOG(3) << "Failed to respond continue: " << status;
    return false;
  }
  return true;
}
}  // namespace

bool SeccompUnotify::IsContinueSupported() {
  static bool supported = []() { return TestUserNotifFlagContinueSupport(); }();
  return supported;
}

SeccompUnotify::SeccompUnotify()
    : SeccompUnotify(std::make_unique<OsSeccompUnotify>()) {}

absl::Status SeccompUnotify::Init(FDCloser seccomp_notify_fd) {
  if (seccomp_notify_fd_.get() > 0) {
    return absl::FailedPreconditionError("Init() must be called only once");
  }
  struct seccomp_notif_sizes sizes = {};
  if (seccomp_unotify_iface_->GetSizes(&sizes) == -1) {
    return absl::InternalError("Couldn't get seccomp_notif_sizes");
  }
  req_size_ = sizes.seccomp_notif;
  req_.reset(static_cast<seccomp_notif*>(malloc(req_size_)));
  resp_size_ = sizes.seccomp_notif_resp;
  resp_.reset(static_cast<seccomp_notif_resp*>(malloc(resp_size_)));
  seccomp_notify_fd_ = std::move(seccomp_notify_fd);
  return absl::OkStatus();
}

absl::StatusOr<seccomp_notif> SeccompUnotify::Receive() {
  if (seccomp_notify_fd_.get() < 0) {
    return absl::FailedPreconditionError("Init() must be called first");
  }
  memset(req_.get(), 0, req_size_);
  if (seccomp_unotify_iface_->ReceiveNotification(seccomp_notify_fd_.get(),
                                                  req_.get()) != 0) {
    if (errno == ENOENT) {
      return absl::NotFoundError("Failed to receive notification");
    }
    return absl::ErrnoToStatus(errno, "Failed to receive notification");
  }
  return *req_;
}

absl::Status SeccompUnotify::Respond(const seccomp_notif& req) {
  resp_->id = req.id;
  if (seccomp_unotify_iface_->SendResponse(seccomp_notify_fd_.get(), *resp_) !=
      0) {
    return absl::ErrnoToStatus(errno, "Failed to send notification");
  }
  return absl::OkStatus();
}

absl::Status SeccompUnotify::RespondErrno(const seccomp_notif& req, int error) {
  if (!resp_) {
    return absl::FailedPreconditionError("Init() must be called first");
  }
  memset(resp_.get(), 0, resp_size_);
  resp_->error = error;
  return Respond(req);
}

absl::Status SeccompUnotify::RespondContinue(const seccomp_notif& req) {
  if (!resp_) {
    return absl::FailedPreconditionError("Init() must be called first");
  }
  memset(resp_.get(), 0, resp_size_);
  resp_->flags = SECCOMP_USER_NOTIF_FLAG_CONTINUE;
  return Respond(req);
}

}  // namespace util
}  // namespace sandbox2
