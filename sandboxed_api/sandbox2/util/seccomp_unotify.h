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

#ifndef SANDBOXED_API_SANDBOX2_UTIL_SECCOMP_UNOTIFY_H_
#define SANDBOXED_API_SANDBOX2_UTIL_SECCOMP_UNOTIFY_H_

#include <linux/seccomp.h>

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "sandboxed_api/sandbox2/syscall.h"
#include "sandboxed_api/util/fileops.h"

#ifndef SECCOMP_IOCTL_NOTIF_RECV
struct seccomp_notif {
  __u64 id;
  __u32 pid;
  __u32 flags;
  struct seccomp_data data;
};

struct seccomp_notif_resp {
  __u64 id;
  __s64 val;
  __s32 error;
  __u32 flags;
};
#endif

namespace sandbox2 {
namespace util {

class SeccompUnotify {
 public:
  // Interface for seccomp_unotify to allow mocking it in tests.
  class SeccompUnotifyInterface {
   public:
    virtual int GetSizes(seccomp_notif_sizes* sizes) = 0;
    virtual int ReceiveNotification(int fd, seccomp_notif* req) = 0;
    virtual int SendResponse(int fd, const seccomp_notif_resp& resp) = 0;
    virtual ~SeccompUnotifyInterface() = default;
  };

  explicit SeccompUnotify();
  explicit SeccompUnotify(
      std::unique_ptr<SeccompUnotifyInterface> seccomp_unotify_iface)
      : seccomp_unotify_iface_(std::move(seccomp_unotify_iface)) {}
  ~SeccompUnotify() = default;

  static bool IsContinueSupported();

  // Initializes the object. Must be called before any other method.
  absl::Status Init(sapi::file_util::fileops::FDCloser seccomp_notify_fd);
  // Receives a notification from the sandboxee.
  absl::StatusOr<seccomp_notif> Receive();
  // Responds to the sandboxee with an errno, syscall is not executed.
  absl::Status RespondErrno(const seccomp_notif& req, int error);
  // Allows the sandboxee to continue execution of the syscall.
  absl::Status RespondContinue(const seccomp_notif& req);
  // Returns the file descriptor of the seccomp notify socket.
  int GetFd() const { return seccomp_notify_fd_.get(); }

 private:
  // Custom deleter for req_ and resp_ members which need to allocate space
  // using malloc.
  struct StdFreeDeleter {
    void operator()(void* p) { std::free(p); }
  };

  absl::Status Respond(const seccomp_notif& req);

  std::unique_ptr<SeccompUnotifyInterface> seccomp_unotify_iface_;
  sapi::file_util::fileops::FDCloser seccomp_notify_fd_;
  size_t req_size_ = 0;
  std::unique_ptr<seccomp_notif, StdFreeDeleter> req_;
  size_t resp_size_ = 0;
  std::unique_ptr<seccomp_notif_resp, StdFreeDeleter> resp_;
};

}  // namespace util
}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_SECCOMP_UNOTIFY_H_
