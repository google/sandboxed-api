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

// Implementation of sandbox2::Comms class.
//
// Warning: This class is not multi-thread safe (for callers). It uses a single
// communications channel (an AF_UNIX socket), so it requires exactly one sender
// and one receiver. If you plan to use it from many threads, provide external
// exclusive locking.

#include "sandboxed_api/sandbox2/comms.h"

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstring>
#include <functional>

#include "google/protobuf/message.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/util/statusor.h"

#ifdef MEMORY_SANITIZER
#include "base/dynamic_annotations.h"
#endif

// Future extension point used to mark code sections that invoke syscalls that
// potentially block.
// Internally at Google, there is an implementation that supports light-weight
// fibers.
class PotentiallyBlockingRegion {
 public:
  ~PotentiallyBlockingRegion() {
    // Do nothing. Not defaulted to avoid "unused variable" warnings.
  }
};

namespace sandbox2 {

namespace {
bool IsFatalError(int saved_errno) {
  return saved_errno != EAGAIN && saved_errno != EWOULDBLOCK &&
         saved_errno != EFAULT && saved_errno != EINTR &&
         saved_errno != EINVAL && saved_errno != ENOMEM;
}
}  // namespace

constexpr uint32_t Comms::kTagBool;
constexpr uint32_t Comms::kTagInt8;
constexpr uint32_t Comms::kTagUint8;
constexpr uint32_t Comms::kTagInt16;
constexpr uint32_t Comms::kTagUint16;
constexpr uint32_t Comms::kTagInt32;
constexpr uint32_t Comms::kTagUint32;
constexpr uint32_t Comms::kTagInt64;
constexpr uint32_t Comms::kTagUint64;
constexpr uint32_t Comms::kTagString;
constexpr uint32_t Comms::kTagBytes;
constexpr uint32_t Comms::kTagProto2;
constexpr uint32_t Comms::kTagFd;

constexpr int Comms::kSandbox2ClientCommsFD;

Comms::Comms(const std::string& socket_name) : socket_name_(socket_name) {}

Comms::Comms(int fd) : connection_fd_(fd) {
  // Generate a unique and meaningful socket name for this FD.
  // Note: getpid()/gettid() are non-blocking syscalls.
  socket_name_ = absl::StrFormat("sandbox2::Comms:FD=%d/PID=%d/TID=%ld", fd,
                                 getpid(), syscall(__NR_gettid));

  // File descriptor is already connected.
  state_ = State::kConnected;
}

Comms::~Comms() { Terminate(); }

int Comms::GetConnectionFD() const {
  return connection_fd_;
}

bool Comms::Listen() {
  if (IsConnected()) {
    return true;
  }

  bind_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);  // Non-blocking
  if (bind_fd_ == -1) {
    SAPI_RAW_PLOG(ERROR, "socket(AF_UNIX)");
    return false;
  }

  sockaddr_un sus;
  socklen_t slen = CreateSockaddrUn(&sus);
  // bind() is non-blocking.
  if (bind(bind_fd_, reinterpret_cast<sockaddr*>(&sus), slen) == -1) {
    SAPI_RAW_PLOG(ERROR, "bind(bind_fd)");

    // Note: checking for EINTR on close() syscall is useless and possibly
    // harmful, see https://lwn.net/Articles/576478/.
    {
      PotentiallyBlockingRegion region;
      close(bind_fd_);
    }
    bind_fd_ = -1;
    return false;
  }

  // listen() non-blocking.
  if (listen(bind_fd_, 0) == -1) {
    SAPI_RAW_PLOG(ERROR, "listen(bind_fd)");
    {
      PotentiallyBlockingRegion region;
      close(bind_fd_);
    }
    bind_fd_ = -1;
    return false;
  }

  SAPI_RAW_VLOG(1, "Listening at: %s", socket_name_);
  return true;
}

bool Comms::Accept() {
  if (IsConnected()) {
    return true;
  }

  sockaddr_un suc;
  socklen_t len = sizeof(suc);
  {
    PotentiallyBlockingRegion region;
    connection_fd_ = TEMP_FAILURE_RETRY(
        accept(bind_fd_, reinterpret_cast<sockaddr*>(&suc), &len));
  }
  if (connection_fd_ == -1) {
    SAPI_RAW_PLOG(ERROR, "accept(bind_fd)");
    {
      PotentiallyBlockingRegion region;
      close(bind_fd_);
    }
    bind_fd_ = -1;
    return false;
  }

  state_ = State::kConnected;

  SAPI_RAW_VLOG(1, "Accepted connection at: %s, fd: %d", socket_name_,
                connection_fd_);
  return true;
}

bool Comms::Connect() {
  if (IsConnected()) {
    return true;
  }

  connection_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);  // Non-blocking
  if (connection_fd_ == -1) {
    SAPI_RAW_PLOG(ERROR, "socket(AF_UNIX)");
    return false;
  }

  sockaddr_un suc;
  socklen_t slen = CreateSockaddrUn(&suc);
  int ret;
  {
    PotentiallyBlockingRegion region;
    ret = TEMP_FAILURE_RETRY(
        connect(connection_fd_, reinterpret_cast<sockaddr*>(&suc), slen));
  }
  if (ret == -1) {
    SAPI_RAW_PLOG(ERROR, "connect(connection_fd)");
    {
      PotentiallyBlockingRegion region;
      close(connection_fd_);
    }
    connection_fd_ = -1;
    return false;
  }

  state_ = State::kConnected;

  SAPI_RAW_VLOG(1, "Connected to: %s, fd: %d", socket_name_, connection_fd_);
  return true;
}

// All member variables touched by Terminate() should be marked as 'mutable'.
void Comms::Terminate() {
  {
    PotentiallyBlockingRegion region;

    state_ = State::kTerminated;

    if (bind_fd_ != -1) {
      close(bind_fd_);
      bind_fd_ = -1;
    }
    if (connection_fd_ != -1) {
      close(connection_fd_);
      connection_fd_ = -1;
    }
  }
}

bool Comms::SendTLV(uint32_t tag, uint64_t length, const uint8_t* bytes) {
  if (length > GetMaxMsgSize()) {
    SAPI_RAW_LOG(ERROR, "Maximum TLV message size exceeded: (%u > %u)", length,
                 GetMaxMsgSize());
    return false;
  }
  if (length > kWarnMsgSize) {
    static int times_warned = 0;
    if (times_warned < 10) {
      ++times_warned;
      SAPI_RAW_LOG(WARNING,
                   "TLV message of size %u detected. Please consider switching "
                   "to Buffer API instead.",
                   length);
    }
  }

  SAPI_RAW_VLOG(3, "Sending a TLV message, tag: 0x%08x, length: %u", tag,
                length);
  {
    absl::MutexLock lock(&tlv_send_transmission_mutex_);
    if (!Send(reinterpret_cast<uint8_t*>(&tag), sizeof(tag))) {
      return false;
    }
    if (!Send(reinterpret_cast<uint8_t*>(&length), sizeof(length))) {
      return false;
    }
    if (length > 0) {
      if (!Send(bytes, length)) {
        return false;
      }
    }
    return true;
  }
}

bool Comms::RecvString(std::string* v) {
  TLV tlv;
  if (!RecvTLV(&tlv)) {
    return false;
  }

  if (tlv.tag != kTagString) {
    SAPI_RAW_LOG(ERROR, "Expected (kTagString == 0x%x), got: 0x%x", kTagString,
                 tlv.tag);
    return false;
  }
  v->assign(reinterpret_cast<const char*>(tlv.value.data()), tlv.value.size());
  return true;
}

bool Comms::SendString(const std::string& v) {
  return SendTLV(kTagString, v.length(),
                 reinterpret_cast<const uint8_t*>(v.c_str()));
}

bool Comms::RecvBytes(std::vector<uint8_t>* buffer) {
  TLV tlv;
  if (!RecvTLV(&tlv)) {
    return false;
  }
  if (tlv.tag != kTagBytes) {
    SAPI_RAW_LOG(ERROR, "Expected (kTagBytes == 0x%x), got: 0x%u", kTagBytes,
                 tlv.tag);
    return false;
  }
  buffer->swap(tlv.value);
  return true;
}

bool Comms::SendBytes(const uint8_t* v, uint64_t len) {
  return SendTLV(kTagBytes, len, v);
}

bool Comms::SendBytes(const std::vector<uint8_t>& buffer) {
  return SendBytes(buffer.data(), buffer.size());
}

bool Comms::RecvCreds(pid_t* pid, uid_t* uid, gid_t* gid) {
  ucred uc;
  socklen_t sls = sizeof(uc);
  int rc;
  {
    // Not completely sure if getsockopt() can block on SO_PEERCRED, but let's
    // play it safe.
    PotentiallyBlockingRegion region;
    rc = getsockopt(GetConnectionFD(), SOL_SOCKET, SO_PEERCRED, &uc, &sls);
  }
  if (rc == -1) {
    SAPI_RAW_PLOG(ERROR, "getsockopt(SO_PEERCRED)");
    return false;
  }
  *pid = uc.pid;
  *uid = uc.uid;
  *gid = uc.gid;

  SAPI_RAW_VLOG(2, "Received credentials from PID/UID/GID: %d/%u/%u", *pid,
                *uid, *gid);
  return true;
}

bool Comms::RecvFD(int* fd) {
  char fd_msg[8192];
  cmsghdr* cmsg = reinterpret_cast<cmsghdr*>(fd_msg);

  InternalTLV tlv;
  iovec iov = {&tlv, sizeof(tlv)};

  msghdr msg;
  msg.msg_name = nullptr;
  msg.msg_namelen = 0;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsg;
  msg.msg_controllen = sizeof(fd_msg);
  msg.msg_flags = 0;

  const auto op = [&msg](int fd) -> ssize_t {
    PotentiallyBlockingRegion region;
    // Use syscall, otherwise we would need to whitelist socketcall() on PPC.
    return TEMP_FAILURE_RETRY(
        util::Syscall(__NR_recvmsg, fd, reinterpret_cast<uintptr_t>(&msg), 0));
  };
  ssize_t len;
  len = op(connection_fd_);
  if (len < 0) {
    if (IsFatalError(errno)) {
      Terminate();
    }
    SAPI_RAW_PLOG(ERROR, "recvmsg(SCM_RIGHTS)");
    return false;
  }
  if (len == 0) {
    Terminate();
    SAPI_RAW_VLOG(1, "RecvFD: end-point terminated the connection.");
    return false;
  }
  if (len != sizeof(tlv)) {
    SAPI_RAW_LOG(ERROR, "Expected size: %u, got %d", sizeof(tlv), len);
    return false;
  }
  // At this point, we know that op() has been called successfully, therefore
  // msg struct has been fully populated. Apparently MSAN is not aware of
  // syscall(__NR_recvmsg) semantics so we need to suppress the error (here and
  // everywhere below).
#ifdef MEMORY_SANITIZER
  ANNOTATE_MEMORY_IS_INITIALIZED(&tlv, sizeof(tlv));
#endif

  if (tlv.tag != kTagFd) {
    SAPI_RAW_LOG(ERROR, "Expected (kTagFD: 0x%x), got: 0x%u", kTagFd, tlv.tag);
    return false;
  }

  cmsg = CMSG_FIRSTHDR(&msg);
#ifdef MEMORY_SANITIZER
  ANNOTATE_MEMORY_IS_INITIALIZED(cmsg, sizeof(cmsghdr));
#endif
  while (cmsg) {
    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
      if (cmsg->cmsg_len != CMSG_LEN(sizeof(int))) {
        SAPI_RAW_VLOG(1,
                      "recvmsg(SCM_RIGHTS): cmsg->cmsg_len != "
                      "CMSG_LEN(sizeof(int)), skipping");
        continue;
      }
      int* fds = reinterpret_cast<int*>(CMSG_DATA(cmsg));
      *fd = fds[0];
#ifdef MEMORY_SANITIZER
      ANNOTATE_MEMORY_IS_INITIALIZED(fd, sizeof(int));
#endif
      return true;
    }
    cmsg = CMSG_NXTHDR(&msg, cmsg);
  }
  SAPI_RAW_LOG(ERROR,
               "Haven't received the SCM_RIGHTS message, process is probably "
               "out of free file descriptors");
  return false;
}

bool Comms::SendFD(int fd) {
  char fd_msg[CMSG_SPACE(sizeof(int))] = {0};
  cmsghdr* cmsg = reinterpret_cast<cmsghdr*>(fd_msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(int));

  int* fds = reinterpret_cast<int*>(CMSG_DATA(cmsg));
  fds[0] = fd;

  InternalTLV tlv = {kTagFd, sizeof(tlv.val), 0};

  iovec iov;
  iov.iov_base = &tlv;
  iov.iov_len = sizeof(tlv);

  msghdr msg;
  msg.msg_name = nullptr;
  msg.msg_namelen = 0;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsg;
  msg.msg_controllen = sizeof(fd_msg);
  msg.msg_flags = 0;

  const auto op = [&msg](int fd) -> ssize_t {
    PotentiallyBlockingRegion region;
    // Use syscall, otherwise we would need to whitelist socketcall() on PPC.
    return TEMP_FAILURE_RETRY(
        util::Syscall(__NR_sendmsg, fd, reinterpret_cast<uintptr_t>(&msg), 0));
  };
  ssize_t len;
  len = op(connection_fd_);
  if (len == -1 && errno == EPIPE) {
    Terminate();
    SAPI_RAW_LOG(ERROR, "sendmsg(SCM_RIGHTS): Peer disconnected");
    return false;
  }
  if (len < 0) {
    if (IsFatalError(errno)) {
      Terminate();
    }
    SAPI_RAW_PLOG(ERROR, "sendmsg(SCM_RIGHTS)");
    return false;
  }
  if (len != sizeof(tlv)) {
    SAPI_RAW_LOG(ERROR, "Expected to send %u bytes, sent %d", sizeof(tlv), len);
    return false;
  }
  return true;
}

bool Comms::RecvProtoBuf(google::protobuf::Message* message) {
  TLV tlv;
  if (!RecvTLV(&tlv)) {
    if (IsConnected()) {
      SAPI_RAW_PLOG(ERROR, "RecvProtoBuf failed for (%s)", socket_name_);
    } else {
      Terminate();
      SAPI_RAW_VLOG(2, "Connection terminated (%s)", socket_name_);
    }
    return false;
  }

  if (tlv.tag != kTagProto2) {
    SAPI_RAW_LOG(ERROR, "Expected tag: 0x%x, got: 0x%u", kTagProto2, tlv.tag);
    return false;
  }
  return message->ParseFromArray(tlv.value.data(), tlv.value.size());
}

bool Comms::SendProtoBuf(const google::protobuf::Message& message) {
  std::string str;
  if (!message.SerializeToString(&str)) {
    SAPI_RAW_LOG(ERROR, "Couldn't serialize the ProtoBuf");
    return false;
  }

  return SendTLV(kTagProto2, str.length(),
                 reinterpret_cast<const uint8_t*>(str.data()));
}

// *****************************************************************************
// All methods below are private, for internal use only.
// *****************************************************************************

socklen_t Comms::CreateSockaddrUn(sockaddr_un* sun) {
  sun->sun_family = AF_UNIX;
  bzero(sun->sun_path, sizeof(sun->sun_path));
  // Create an 'abstract socket address' by specifying a leading null byte. The
  // remainder of the path is used as a unique name, but no file is created on
  // the filesystem. No need to NUL-terminate the string.
  // See `man 7 unix` for further explanation.
  strncpy(&sun->sun_path[1], socket_name_.c_str(), sizeof(sun->sun_path) - 1);

  // Len is complicated - it's essentially size of the path, plus initial
  // NUL-byte, minus size of the sun.sun_family.
  socklen_t slen = sizeof(sun->sun_family) + strlen(socket_name_.c_str()) + 1;
  if (slen > sizeof(sockaddr_un)) {
    slen = sizeof(sockaddr_un);
  }
  return slen;
}

bool Comms::Send(const uint8_t* bytes, uint64_t len) {
  uint64_t total_sent = 0;
  const auto op = [bytes, len, &total_sent](int fd) -> ssize_t {
    PotentiallyBlockingRegion region;
    return TEMP_FAILURE_RETRY(write(fd, &bytes[total_sent], len - total_sent));
  };
  while (total_sent < len) {
    ssize_t s;
    s = op(connection_fd_);
    if (s == -1 && errno == EPIPE) {
      Terminate();
      // We do not expect the other end to disappear.
      SAPI_RAW_LOG(ERROR, "Send: end-point terminated the connection");
      return false;
    }
    if (s == -1) {
      if (IsFatalError(errno)) {
        Terminate();
      }
      SAPI_RAW_PLOG(ERROR, "write");
      return false;
    }
    if (s == 0) {
      SAPI_RAW_LOG(ERROR, "Couldn't write more bytes, wrote: %u, requested: %u",
                   total_sent, len);
      return false;
    }
    total_sent += s;
  }
  return true;
}

bool Comms::Recv(uint8_t* bytes, uint64_t len) {
  uint64_t total_recv = 0;
  const auto op = [bytes, len, &total_recv](int fd) -> ssize_t {
    PotentiallyBlockingRegion region;
    return TEMP_FAILURE_RETRY(read(fd, &bytes[total_recv], len - total_recv));
  };
  while (total_recv < len) {
    ssize_t s;
    s = op(connection_fd_);
    if (s == -1) {
      if (IsFatalError(errno)) {
        Terminate();
      }
      SAPI_RAW_PLOG(ERROR, "read");
      return false;
    }
    if (s == 0) {
      Terminate();
      // The other end might have finished its work.
      SAPI_RAW_VLOG(2, "Recv: end-point terminated the connection.");
      return false;
    }
    total_recv += s;
  }
  return true;
}

// Internal helper method (low level).
bool Comms::RecvTL(uint32_t* tag, uint64_t* length) {
  if (!Recv(reinterpret_cast<uint8_t*>(tag), sizeof(*tag))) {
    return false;
  }
  if (!Recv(reinterpret_cast<uint8_t*>(length), sizeof(*length))) {
    return false;
  }
  if (*length > GetMaxMsgSize()) {
    SAPI_RAW_LOG(ERROR, "Maximum TLV message size exceeded: (%u > %d)", *length,
                 GetMaxMsgSize());
    return false;
  }
  if (*length > kWarnMsgSize) {
    static int times_warned = 0;
    if (times_warned < 10) {
      ++times_warned;
      SAPI_RAW_LOG(
          WARNING,
          "TLV message of size: (%u detected. Please consider switching to "
          "Buffer API instead.",
          *length);
    }
  }
  return true;
}

bool Comms::RecvTLV(TLV* tlv) {
  absl::MutexLock lock(&tlv_recv_transmission_mutex_);
  uint64_t length;
  if (!RecvTL(&tlv->tag, &length)) {
    return false;
  }

  tlv->value.resize(length);
  return length == 0 || Recv(tlv->value.data(), length);
}

bool Comms::RecvTLV(uint32_t* tag, std::vector<uint8_t>* value) {
  absl::MutexLock lock(&tlv_recv_transmission_mutex_);
  uint64_t length;
  if (!RecvTL(tag, &length)) {
    return false;
  }

  value->resize(length);
  return length == 0 || Recv(value->data(), length);
}

bool Comms::RecvTLV(uint32_t* tag, uint64_t* length, void* buffer,
                    uint64_t buffer_size) {
  absl::MutexLock lock(&tlv_recv_transmission_mutex_);
  if (!RecvTL(tag, length)) {
    return false;
  }

  if (*length > buffer_size) {
    SAPI_RAW_LOG(ERROR, "Buffer size too small (0x%x > 0x%x)", *length,
                 buffer_size);
    return false;
  } else if (*length > 0) {
    if (!Recv(reinterpret_cast<uint8_t*>(buffer), *length)) {
      return false;
    }
  }
  return true;
}

bool Comms::RecvInt(void* buffer, uint64_t len, uint32_t tag) {
  uint32_t received_tag;
  uint64_t received_length;
  if (!RecvTLV(&received_tag, &received_length, buffer, len)) {
    return false;
  }

  if (received_tag != tag) {
    SAPI_RAW_LOG(ERROR, "Expected tag: 0x%08x, got: 0x%x", tag, received_tag);
    return false;
  }
  if (received_length != len) {
    SAPI_RAW_LOG(ERROR, "Expected length: %u, got: %u", len, received_length);
    return false;
  }
  return true;
}

}  // namespace sandbox2
