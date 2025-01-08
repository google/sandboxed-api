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

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/dynamic_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/message_lite.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status.h"
#include "sandboxed_api/util/status.pb.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {

class PotentiallyBlockingRegion {
 public:
  ~PotentiallyBlockingRegion() {
    // Do nothing. Not defaulted to avoid "unused variable" warnings.
  }
};
namespace {

using sapi::file_util::fileops::FDCloser;

bool IsFatalError(int saved_errno) {
  return saved_errno != EAGAIN && saved_errno != EWOULDBLOCK &&
         saved_errno != EFAULT && saved_errno != EINTR &&
         saved_errno != EINVAL && saved_errno != ENOMEM;
}

int GetDefaultCommsFd() {
  if (const char* var = getenv(Comms::kSandbox2CommsFDEnvVar); var) {
    int fd;
    SAPI_RAW_CHECK(absl::SimpleAtoi(var, &fd), "cannot parse comms fd var");
    unsetenv(Comms::kSandbox2CommsFDEnvVar);
    return fd;
  }
  return Comms::kSandbox2ClientCommsFD;
}

socklen_t CreateSockaddrUn(const std::string& socket_name, bool abstract_uds,
                           sockaddr_un* sun) {
  sun->sun_family = AF_UNIX;
  bzero(sun->sun_path, sizeof(sun->sun_path));
  socklen_t slen = sizeof(sun->sun_family) + strlen(socket_name.c_str());
  if (abstract_uds) {
    // Create an 'abstract socket address' by specifying a leading null byte.
    // The remainder of the path is used as a unique name, but no file is
    // created on the filesystem. No need to NUL-terminate the string. See `man
    // 7 unix` for further explanation.
    strncpy(&sun->sun_path[1], socket_name.c_str(), sizeof(sun->sun_path) - 1);
    // Len is complicated - it's essentially size of the path, plus initial
    // NUL-byte, minus size of the sun.sun_family.
    slen++;
  } else {
    // Create the socket address as it was passed from the constructor.
    strncpy(&sun->sun_path[0], socket_name.c_str(), sizeof(sun->sun_path));
  }

  // This takes care of the socket address overflow.
  if (slen > sizeof(sockaddr_un)) {
    SAPI_RAW_LOG(ERROR, "Socket address is too long, will be truncated");
    slen = sizeof(sockaddr_un);
  }
  return slen;
}
}  // namespace

Comms::Comms(int fd, absl::string_view name) : raw_comms_(RawCommsFdImpl(fd)) {
  // Generate a unique and meaningful socket name for this FD.
  // Note: getpid()/gettid() are non-blocking syscalls.
  if (name.empty()) {
    name_ = absl::StrFormat("sandbox2::Comms:FD=%d/PID=%d/TID=%ld", fd,
                            getpid(), syscall(__NR_gettid));
  } else {
    name_ = std::string(name);
  }

  // File descriptor is already connected.
  state_ = State::kConnected;
}

Comms::Comms(Comms::DefaultConnectionTag) : Comms(GetDefaultCommsFd()) {}

Comms::~Comms() { Terminate(); }

int Comms::GetConnectionFD() const {
  return GetRawComms() == nullptr ? -1 : GetRawComms()->GetConnectionFD();
}

absl::StatusOr<ListeningComms> ListeningComms::Create(
    absl::string_view socket_name, bool abstract_uds) {
  ListeningComms comms(std::string(socket_name), abstract_uds);
  SAPI_RETURN_IF_ERROR(comms.Listen());
  return comms;
}

absl::Status ListeningComms::Listen() {
  bind_fd_ = FDCloser(socket(AF_UNIX, SOCK_STREAM, 0));  // Non-blocking
  if (bind_fd_.get() == -1) {
    return absl::ErrnoToStatus(errno, "socket(AF_UNIX) failed");
  }

  sockaddr_un sus;
  socklen_t slen = CreateSockaddrUn(socket_name_, abstract_uds_, &sus);
  // bind() is non-blocking.
  if (bind(bind_fd_.get(), reinterpret_cast<sockaddr*>(&sus), slen) == -1) {
    return absl::ErrnoToStatus(errno, "bind failed");
  }

  // listen() non-blocking.
  if (listen(bind_fd_.get(), 0) == -1) {
    return absl::ErrnoToStatus(errno, "listen failed");
  }

  SAPI_RAW_VLOG(1, "Listening at: %s", socket_name_.c_str());
  return absl::OkStatus();
}

absl::StatusOr<Comms> ListeningComms::Accept() {
  sockaddr_un suc;
  socklen_t len = sizeof(suc);
  int connection_fd;
  {
    PotentiallyBlockingRegion region;
    connection_fd = TEMP_FAILURE_RETRY(
        accept(bind_fd_.get(), reinterpret_cast<sockaddr*>(&suc), &len));
  }
  if (connection_fd == -1) {
    return absl::ErrnoToStatus(errno, "accept failed");
  }
  SAPI_RAW_VLOG(1, "Accepted connection at: %s, fd: %d", socket_name_.c_str(),
                connection_fd);
  return Comms(connection_fd, socket_name_);
}

absl::StatusOr<Comms> Comms::Connect(const std::string& socket_name,
                                     bool abstract_uds) {
  FDCloser connection_fd(socket(AF_UNIX, SOCK_STREAM, 0));  // Non-blocking
  if (connection_fd.get() == -1) {
    return absl::ErrnoToStatus(errno, "socket(AF_UNIX)");
  }

  sockaddr_un suc;
  socklen_t slen = CreateSockaddrUn(socket_name, abstract_uds, &suc);
  int ret;
  {
    PotentiallyBlockingRegion region;
    ret = TEMP_FAILURE_RETRY(
        connect(connection_fd.get(), reinterpret_cast<sockaddr*>(&suc), slen));
  }
  if (ret == -1) {
    return absl::ErrnoToStatus(errno, "connect(connection_fd)");
  }

  SAPI_RAW_VLOG(1, "Connected to: %s, fd: %d", socket_name.c_str(),
                connection_fd.get());
  return Comms(connection_fd.Release(), socket_name);
}

void Comms::Terminate() {
  state_ = State::kTerminated;

  raw_comms_ = std::unique_ptr<RawComms>();
  listening_comms_.reset();
}

bool Comms::SendTLV(uint32_t tag, size_t length, const void* value) {
  if (length > GetMaxMsgSize()) {
    SAPI_RAW_LOG(ERROR, "Maximum TLV message size exceeded: (%zu > %zu)",
                 length, GetMaxMsgSize());
    return false;
  }
  if (length > kWarnMsgSize) {
    // TODO(cblichmann): Use LOG_FIRST_N once Abseil logging is released.
    static std::atomic<int> times_warned = 0;
    if (times_warned.fetch_add(1, std::memory_order_relaxed) < 10) {
      SAPI_RAW_LOG(
          WARNING,
          "TLV message of size %zu detected. Please consider switching "
          "to Buffer API instead.",
          length);
    }
  }

  SAPI_RAW_VLOG(3, "Sending a TLV message, tag: 0x%08x, length: %zu", tag,
                length);

  // To maintain consistency with `RecvTL()`, we wrap `tag` and `length` in a TL
  // struct.
  const InternalTLV tl = {
      .tag = tag,
      .len = length,
  };

  const size_t inline_size =
      std::min(length, kSendTLVTempBufferSize - sizeof(tl));
  uint8_t tlv[kSendTLVTempBufferSize];
  memcpy(tlv, &tl, sizeof(tl));
  memcpy(&tlv[sizeof(tl)], value, inline_size);
  if (!Send(&tlv, sizeof(tl) + inline_size)) {
    return false;
  }
  if (inline_size < length) {
    return Send(reinterpret_cast<const uint8_t*>(value) + inline_size,
                length - inline_size);
  }
  return true;
}

bool Comms::RecvString(std::string* v) {
  uint32_t tag;
  if (!RecvTLV(&tag, v)) {
    return false;
  }

  if (tag != kTagString) {
    v->clear();
    SAPI_RAW_LOG(ERROR, "Expected (kTagString == 0x%x), got: 0x%x", kTagString,
                 tag);
    return false;
  }
  return true;
}

bool Comms::SendString(const std::string& v) {
  return SendTLV(kTagString, v.length(), v.c_str());
}

bool Comms::RecvBytes(std::vector<uint8_t>* buffer) {
  uint32_t tag;
  if (!RecvTLV(&tag, buffer)) {
    return false;
  }
  if (tag != kTagBytes) {
    buffer->clear();
    SAPI_RAW_LOG(ERROR, "Expected (kTagBytes == 0x%x), got: 0x%u", kTagBytes,
                 tag);
    return false;
  }
  return true;
}

bool Comms::SendBytes(const uint8_t* v, size_t len) {
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
  iovec iov = {.iov_base = &tlv, .iov_len = sizeof(tlv)};

  msghdr msg = {
      .msg_name = nullptr,
      .msg_namelen = 0,
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = cmsg,
      .msg_controllen = sizeof(fd_msg),
      .msg_flags = 0,
  };

  if (GetRawComms() == nullptr) {
    SAPI_RAW_LOG(ERROR, "RecvFD: connection terminated");
    return false;
  }

  ssize_t len = GetRawComms()->RawRecvMsg(&msg);
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
    SAPI_RAW_LOG(ERROR, "Expected size: %zu, got %zd", sizeof(tlv), len);
    return false;
  }
  // At this point, we know that op() has been called successfully, therefore
  // msg struct has been fully populated. Apparently MSAN is not aware of
  // syscall(__NR_recvmsg) semantics so we need to suppress the error (here and
  // everywhere below).
  ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(&tlv, sizeof(tlv));

  if (tlv.tag != kTagFd) {
    SAPI_RAW_LOG(ERROR, "Expected (kTagFD: 0x%x), got: 0x%x", kTagFd, tlv.tag);
    return false;
  }

  cmsg = CMSG_FIRSTHDR(&msg);
  ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(cmsg, sizeof(cmsghdr));
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
      ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(fd, sizeof(int));
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

  InternalTLV tlv = {kTagFd, 0};

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

  if (GetRawComms() == nullptr) {
    SAPI_RAW_LOG(ERROR, "SendFD: connection terminated");
    return false;
  }

  ssize_t len = GetRawComms()->RawSendMsg(&msg);
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
    SAPI_RAW_LOG(ERROR, "Expected to send %zu bytes, sent %zd", sizeof(tlv),
                 len);
    return false;
  }
  return true;
}

bool Comms::RecvProtoBuf(google::protobuf::MessageLite* message) {
  uint32_t tag;
  std::vector<uint8_t> bytes;
  if (!RecvTLV(&tag, &bytes)) {
    if (IsConnected()) {
      SAPI_RAW_PLOG(ERROR, "RecvProtoBuf failed for (%s)", name_);
    } else {
      Terminate();
      SAPI_RAW_VLOG(2, "Connection terminated (%s)", name_.c_str());
    }
    return false;
  }

  if (tag != kTagProto2) {
    SAPI_RAW_LOG(ERROR, "Expected tag: 0x%x, got: 0x%u", kTagProto2, tag);
    return false;
  }
  return message->ParseFromArray(bytes.data(), bytes.size());
}

bool Comms::SendProtoBuf(const google::protobuf::MessageLite& message) {
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

int Comms::RawCommsFdImpl::GetConnectionFD() const {
  return connection_fd_.get();
}

void Comms::RawCommsFdImpl::MoveToAnotherFd() {
  SAPI_RAW_CHECK(connection_fd_.get() != -1,
                 "Cannot move comms fd as it's not connected");
  FDCloser new_fd(dup(connection_fd_.get()));
  SAPI_RAW_CHECK(new_fd.get() != -1, "Failed to move comms to another fd");
  connection_fd_.Swap(new_fd);
}

ssize_t Comms::RawCommsFdImpl::RawSend(const void* data, size_t len) {
  PotentiallyBlockingRegion region;
  return TEMP_FAILURE_RETRY(write(connection_fd_.get(), data, len));
}

ssize_t Comms::RawCommsFdImpl::RawRecv(void* data, size_t len) {
  PotentiallyBlockingRegion region;
  return TEMP_FAILURE_RETRY(read(connection_fd_.get(), data, len));
}

ssize_t Comms::RawCommsFdImpl::RawSendMsg(const void* msg) {
  PotentiallyBlockingRegion region;
  // Use syscall, otherwise we would need to allow socketcall() on PPC.
  return TEMP_FAILURE_RETRY(util::Syscall(__NR_sendmsg, connection_fd_.get(),
                                          reinterpret_cast<uintptr_t>(msg), 0));
}

ssize_t Comms::RawCommsFdImpl::RawRecvMsg(void* msg) {
  PotentiallyBlockingRegion region;
  // Use syscall, otherwise we would need to allow socketcall() on PPC.
  return TEMP_FAILURE_RETRY(util::Syscall(__NR_recvmsg, connection_fd_.get(),
                                          reinterpret_cast<uintptr_t>(msg), 0));
}

bool Comms::Send(const void* data, size_t len) {
  if (GetRawComms() == nullptr) {
    SAPI_RAW_LOG(ERROR, "Send: connection terminated");
    return false;
  }

  size_t total_sent = 0;
  const char* bytes = reinterpret_cast<const char*>(data);
  while (total_sent < len) {
    ssize_t s = GetRawComms()->RawSend(&bytes[total_sent], len - total_sent);
    if (s == -1 && errno == EPIPE) {
      Terminate();
      // We do not expect the other end to disappear.
      SAPI_RAW_LOG(ERROR, "Send: end-point terminated the connection");
      return false;
    }
    if (s == -1) {
      SAPI_RAW_PLOG(ERROR, "write");
      if (IsFatalError(errno)) {
        Terminate();
      }
      return false;
    }
    if (s == 0) {
      SAPI_RAW_LOG(ERROR,
                   "Couldn't write more bytes, wrote: %zu, requested: %zu",
                   total_sent, len);
      return false;
    }
    total_sent += s;
  }
  return true;
}

bool Comms::Recv(void* data, size_t len) {
  if (GetRawComms() == nullptr) {
    SAPI_RAW_LOG(ERROR, "Recv: connection terminated");
    return false;
  }

  size_t total_recv = 0;
  char* bytes = reinterpret_cast<char*>(data);
  while (total_recv < len) {
    ssize_t s = GetRawComms()->RawRecv(&bytes[total_recv], len - total_recv);
    if (s == -1) {
      SAPI_RAW_PLOG(ERROR, "read");
      if (IsFatalError(errno)) {
        Terminate();
      }
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
bool Comms::RecvTL(uint32_t* tag, size_t* length) {
  InternalTLV tl;
  if (!Recv(reinterpret_cast<uint8_t*>(&tl), sizeof(tl))) {
    SAPI_RAW_VLOG(2, "RecvTL: Can't read tag and length");
    return false;
  }
  *tag = tl.tag;
  *length = tl.len;
  if (*length > GetMaxMsgSize()) {
    SAPI_RAW_LOG(ERROR, "Maximum TLV message size exceeded: (%zu > %zd)",
                 *length, GetMaxMsgSize());
    return false;
  }
  if (*length > kWarnMsgSize) {
    static std::atomic<int> times_warned = 0;
    if (times_warned.fetch_add(1, std::memory_order_relaxed) < 10) {
      SAPI_RAW_LOG(
          WARNING,
          "TLV message of size: %zu detected. Please consider switching to "
          "Buffer API instead.",
          *length);
    }
  }
  return true;
}

bool Comms::RecvTLV(uint32_t* tag, std::vector<uint8_t>* value) {
  return RecvTLVGeneric(tag, value);
}

bool Comms::RecvTLV(uint32_t* tag, std::string* value) {
  return RecvTLVGeneric(tag, value);
}

template <typename T>
bool Comms::RecvTLVGeneric(uint32_t* tag, T* value) {
  size_t length;
  if (!RecvTL(tag, &length)) {
    return false;
  }

  value->resize(length);
  return length == 0 || Recv(reinterpret_cast<uint8_t*>(value->data()), length);
}

bool Comms::RecvTLV(uint32_t* tag, size_t* length, void* buffer,
                    size_t buffer_size, std::optional<uint32_t> expected_tag) {
  if (!RecvTL(tag, length)) {
    return false;
  }

  if (expected_tag.has_value() && *tag != *expected_tag) {
    SAPI_RAW_LOG(ERROR, "Expected tag: 0x%08x, got: 0x%x", *expected_tag, *tag);
    return false;
  }

  if (*length == 0) {
    return true;
  }

  if (*length > buffer_size) {
    SAPI_RAW_LOG(ERROR, "Buffer size too small (0x%zx > 0x%zx)", *length,
                 buffer_size);
    return false;
  }

  return Recv(reinterpret_cast<uint8_t*>(buffer), *length);
}

bool Comms::RecvInt(void* buffer, size_t len, uint32_t tag) {
  uint32_t received_tag;
  size_t received_length;
  if (!RecvTLV(&received_tag, &received_length, buffer, len, tag)) {
    return false;
  }

  if (received_length != len) {
    SAPI_RAW_LOG(ERROR, "Expected length: %zu, got: %zu", len, received_length);
    return false;
  }
  return true;
}

bool Comms::RecvStatus(absl::Status* status) {
  sapi::StatusProto proto;
  if (!RecvProtoBuf(&proto)) {
    return false;
  }
  *status = sapi::MakeStatusFromProto(proto);
  return true;
}

bool Comms::SendStatus(const absl::Status& status) {
  sapi::StatusProto proto;
  sapi::SaveStatusToProto(status, &proto);
  return SendProtoBuf(proto);
}

void Comms::MoveToAnotherFd() {
  SAPI_RAW_CHECK(GetRawComms() != nullptr,
                 "Cannot move comms fd as it's not connected");
  GetRawComms()->MoveToAnotherFd();
}

}  // namespace sandbox2
