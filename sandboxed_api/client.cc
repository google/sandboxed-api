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

#include <dlfcn.h>
#include <syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkingclient.h"
#include "sandboxed_api/sandbox2/logsink.h"
#include "sandboxed_api/var_type.h"

namespace sapi {

namespace client {

void HandleCallMsg(const FuncCall& call, FuncRet* ret);

// Handles requests to allocate memory inside the sandboxee.
void HandleAllocMsg(const size_t size, FuncRet* ret) {
  VLOG(1) << "HandleAllocMsg: size=" << size;

  const void* allocated = malloc(size);

  ret->ret_type = v::Type::kPointer;
  ret->int_val = reinterpret_cast<uintptr_t>(allocated);
  ret->success = true;
}

// Like HandleAllocMsg(), but handles requests to reallocate memory.
void HandleReallocMsg(uintptr_t ptr, size_t size, FuncRet* ret) {
  VLOG(1) << "HandleReallocMsg(" << absl::StrCat(absl::Hex(ptr)) << ", " << size
          << ")";

  const void* reallocated = realloc(reinterpret_cast<void*>(ptr), size);

  ret->ret_type = v::Type::kPointer;
  ret->int_val = reinterpret_cast<uintptr_t>(reallocated);
  ret->success = true;
}

void HandleMarkMemoryInit(uintptr_t ptr, size_t size, FuncRet* ret) {
  ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(reinterpret_cast<void*>(ptr), size);

  ret->ret_type = v::Type::kVoid;
  ret->success = true;
  ret->int_val = 0ULL;
}

// Handles requests to free memory previously allocated by HandleAllocMsg() and
// HandleReallocMsg().
void HandleFreeMsg(uintptr_t ptr, FuncRet* ret) {
  VLOG(1) << "HandleFreeMsg: free(0x" << absl::StrCat(absl::Hex(ptr)) << ")";

  free(reinterpret_cast<void*>(ptr));
  ret->ret_type = v::Type::kVoid;
  ret->success = true;
  ret->int_val = 0ULL;
}

// Handles requests to find a symbol value.
void HandleSymbolMsg(const char* symname, FuncRet* ret) {
  ret->ret_type = v::Type::kPointer;

  void* handle = dlopen(nullptr, RTLD_NOW);
  if (handle == nullptr) {
    ret->success = false;
    ret->int_val = static_cast<uintptr_t>(Error::kDlOpen);
    return;
  }

  ret->int_val = reinterpret_cast<uintptr_t>(dlsym(handle, symname));
  ret->success = true;
}

// Handles requests to receive a file descriptor from sandboxer.
void HandleSendFd(sandbox2::Comms* comms, FuncRet* ret) {
  ret->ret_type = v::Type::kInt;
  int fd = -1;

  if (comms->RecvFD(&fd) == false) {
    ret->success = false;
    return;
  }

  ret->int_val = fd;
  ret->success = true;
}

// Handles requests to send a file descriptor back to sandboxer.
void HandleRecvFd(sandbox2::Comms* comms, int fd_to_transfer, FuncRet* ret) {
  ret->ret_type = v::Type::kVoid;

  if (comms->SendFD(fd_to_transfer) == false) {
    ret->success = false;
    return;
  }

  ret->success = true;
}

// Handles requests to close a file descriptor in the sandboxee.
void HandleCloseFd(sandbox2::Comms* comms, int fd_to_close, FuncRet* ret) {
  VLOG(1) << "HandleCloseFd: close(" << fd_to_close << ")";
  close(fd_to_close);

  ret->ret_type = v::Type::kVoid;
  ret->success = true;
}

void HandleStrlen(sandbox2::Comms* comms, const char* ptr, FuncRet* ret) {
  ret->ret_type = v::Type::kInt;
  ret->int_val = strlen(ptr);
  ret->success = true;
}

template <typename T>
static T BytesAs(const std::vector<uint8_t>& bytes) {
  static_assert(std::is_trivial<T>(),
                "only trivial types can be used with BytesAs");
  CHECK_EQ(bytes.size(), sizeof(T));
  T rv;
  memcpy(&rv, bytes.data(), sizeof(T));
  return rv;
}

void ServeRequest(sandbox2::Comms* comms) {
  uint32_t tag;
  std::vector<uint8_t> bytes;

  CHECK(comms->RecvTLV(&tag, &bytes));

  FuncRet ret{};  // Brace-init zeroes struct padding

  switch (tag) {
    case comms::kMsgCall:
      VLOG(1) << "Client::kMsgCall";
      HandleCallMsg(BytesAs<FuncCall>(bytes), &ret);
      break;
    case comms::kMsgAllocate:
      VLOG(1) << "Client::kMsgAllocate";
      HandleAllocMsg(BytesAs<size_t>(bytes), &ret);
      break;
    case comms::kMsgReallocate:
      VLOG(1) << "Client::kMsgReallocate";
      {
        auto req = BytesAs<comms::ReallocRequest>(bytes);
        HandleReallocMsg(req.old_addr, req.size, &ret);
      }
      break;
    case comms::kMsgFree:
      VLOG(1) << "Client::kMsgFree";
      HandleFreeMsg(BytesAs<uintptr_t>(bytes), &ret);
      break;
    case comms::kMsgSymbol:
      CHECK_EQ(bytes.size(),
               1 + std::distance(bytes.begin(),
                                 std::find(bytes.begin(), bytes.end(), '\0')));
      VLOG(1) << "Received Client::kMsgSymbol message";
      HandleSymbolMsg(reinterpret_cast<const char*>(bytes.data()), &ret);
      break;
    case comms::kMsgExit:
      VLOG(1) << "Received Client::kMsgExit message";
      syscall(__NR_exit_group, 0UL);
      break;
    case comms::kMsgSendFd:
      VLOG(1) << "Received Client::kMsgSendFd message";
      HandleSendFd(comms, &ret);
      break;
    case comms::kMsgRecvFd:
      VLOG(1) << "Received Client::kMsgRecvFd message";
      HandleRecvFd(comms, BytesAs<int>(bytes), &ret);
      break;
    case comms::kMsgClose:
      VLOG(1) << "Received Client::kMsgClose message";
      HandleCloseFd(comms, BytesAs<int>(bytes), &ret);
      break;
    case comms::kMsgStrlen:
      VLOG(1) << "Received Client::kMsgStrlen message";
      HandleStrlen(comms, BytesAs<const char*>(bytes), &ret);
      break;
    case comms::kMsgMarkMemoryInit:
      VLOG(1) << "Received Client::kMsgMarkMemoryInit message";
      {
        auto req = BytesAs<comms::ReallocRequest>(bytes);
        HandleMarkMemoryInit(req.old_addr, req.size, &ret);
      }
      break;
    default:
      LOG(FATAL) << "Received unknown tag: " << tag;
      break;  // Not reached
  }

  if (ret.ret_type == v::Type::kFloat) {
    // Make MSAN happy with long double.
    ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(&ret.float_val, sizeof(ret.float_val));
    VLOG(1) << "Returned value: " << ret.float_val
            << ", Success: " << (ret.success ? "Yes" : "No");
  } else {
    VLOG(1) << "Returned value: " << ret.int_val << " (0x"
            << absl::StrCat(absl::Hex(ret.int_val))
            << "), Success: " << (ret.success ? "Yes" : "No");
  }

  CHECK(comms->SendTLV(comms::kMsgReturn, sizeof(ret),
                       reinterpret_cast<uint8_t*>(&ret)));
}

}  // namespace client
}  // namespace sapi

ABSL_ATTRIBUTE_WEAK int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  // Note regarding the FD usage here: Parent and child seem to make use of the
  // same FD, although this is not true. During process setup `dup2()` will be
  // called to replace the FD `kSandbox2ClientCommsFD`.
  // We do not use a new comms object here as the destructor would close our FD.
  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  sandbox2::ForkingClient s2client(&comms);

  // Forkserver loop.
  while (true) {
    pid_t pid = s2client.WaitAndFork();
    if (pid == -1) {
      LOG(FATAL) << "Could not spawn a new sandboxee";
    }
    if (pid == 0) {
      break;
    }
  }

  // Child thread.
  s2client.SandboxMeHere();

  // Enable log forwarding if enabled by the sandboxer.
  if (s2client.HasMappedFD(sandbox2::LogSink::kLogFDName)) {
    s2client.SendLogsToSupervisor();
  }

  // Run SAPI stub.
  while (true) {
    sapi::client::ServeRequest(&comms);
  }
  LOG(FATAL) << "Unreachable";
}
