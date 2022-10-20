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

#include "sandboxed_api/sandbox2/client.h"

#include <dlfcn.h>
#include <sys/syscall.h>

#include <cstring>
#include <iterator>
#include <list>
#include <vector>

#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "absl/base/dynamic_annotations.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/lenval_core.h"
#include "sandboxed_api/proto_arg.pb.h"
#include "sandboxed_api/proto_helper.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkingclient.h"
#include "sandboxed_api/sandbox2/logsink.h"
#include "sandboxed_api/util/raw_logging.h"

#include <ffi.h>

namespace sapi {
namespace {

// Guess the FFI type on the basis of data size and float/non-float/bool.
ffi_type* GetFFIType(size_t size, v::Type type) {
  switch (type) {
    case v::Type::kVoid:
      return &ffi_type_void;
    case v::Type::kPointer:
      return &ffi_type_pointer;
    case v::Type::kFd:
      return &ffi_type_sint;
    case v::Type::kFloat:
      if (size == sizeof(float)) {
        return &ffi_type_float;
      }
      if (size == sizeof(double)) {
        return &ffi_type_double;
      }
      if (size == sizeof(long double)) {
        return &ffi_type_longdouble;
      }
      LOG(FATAL) << "Unsupported floating-point size: " << size;
    case v::Type::kInt:
      switch (size) {
        case 1:
          return &ffi_type_uint8;
        case 2:
          return &ffi_type_uint16;
        case 4:
          return &ffi_type_uint32;
        case 8:
          return &ffi_type_uint64;
        default:
          LOG(FATAL) << "Unsupported integral size: " << size;
      }
    case v::Type::kStruct:
      LOG(FATAL) << "Structs are not supported as function arguments";
    case v::Type::kProto:
      LOG(FATAL) << "Protos are not supported as function arguments";
    default:
      LOG(FATAL) << "Unknown type: " << type << " of size: " << size;
  }
}

// Provides an interface to prepare the arguments for a function call.
// In case of protobuf arguments, the class allocates and manages
// memory for the deserialized protobuf.
class FunctionCallPreparer {
 public:
  explicit FunctionCallPreparer(const FuncCall& call) {
    CHECK(call.argc <= FuncCall::kArgsMax)
        << "Number of arguments of a sandbox call exceeds limits.";
    for (int i = 0; i < call.argc; ++i) {
      arg_types_[i] = GetFFIType(call.arg_size[i], call.arg_type[i]);
    }
    ret_type_ = GetFFIType(call.ret_size, call.ret_type);
    for (int i = 0; i < call.argc; ++i) {
      if (call.arg_type[i] == v::Type::kPointer &&
          call.aux_type[i] == v::Type::kProto) {
        // Deserialize protobuf stored in the LenValueStruct and keep a
        // reference to both. This way we are able to update the content of the
        // LenValueStruct (when the sandboxee modifies the protobuf).
        // This will also make sure that the protobuf is freed afterwards.
        arg_values_[i] = GetDeserializedProto(
            reinterpret_cast<LenValStruct*>(call.args[i].arg_int));
      } else if (call.arg_type[i] == v::Type::kFloat) {
        arg_values_[i] = reinterpret_cast<const void*>(&call.args[i].arg_float);
      } else {
        arg_values_[i] = reinterpret_cast<const void*>(&call.args[i].arg_int);
      }
    }
  }

  ~FunctionCallPreparer() {
    for (const auto& idx_proto : protos_to_be_destroyed_) {
      const auto proto = idx_proto.second;
      LenValStruct* lvs = idx_proto.first;
      // There is no way to figure out whether the protobuf structure has
      // changed or not, so we always serialize the protobuf again and replace
      // the LenValStruct content.
      std::vector<uint8_t> serialized = SerializeProto(*proto).value();
      // Reallocate the LV memory to match its length.
      if (lvs->size != serialized.size()) {
        void* newdata = realloc(lvs->data, serialized.size());
        if (!newdata) {
          LOG(FATAL) << "Failed to reallocate protobuf buffer (size="
                     << serialized.size() << ")";
        }
        lvs->size = serialized.size();
        lvs->data = newdata;
      }
      memcpy(lvs->data, serialized.data(), serialized.size());

      delete proto;
    }
  }

  ffi_type* ret_type() const { return ret_type_; }
  ffi_type** arg_types() const { return const_cast<ffi_type**>(arg_types_); }
  void** arg_values() const { return const_cast<void**>(arg_values_); }

 private:
  // Deserializes the protobuf argument.
  google::protobuf::MessageLite** GetDeserializedProto(LenValStruct* src) {
    ProtoArg proto_arg;
    if (!proto_arg.ParseFromArray(src->data, src->size)) {
      LOG(FATAL) << "Unable to parse ProtoArg.";
    }
    const google::protobuf::Descriptor* desc =
        google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(
            proto_arg.full_name());
    LOG_IF(FATAL, desc == nullptr) << "Unable to find the descriptor for '"
                                   << proto_arg.full_name() << "'" << desc;
    google::protobuf::MessageLite* deserialized_proto =
        google::protobuf::MessageFactory::generated_factory()->GetPrototype(desc)->New();
    LOG_IF(FATAL, deserialized_proto == nullptr)
        << "Unable to create deserialized proto for " << proto_arg.full_name();
    if (!deserialized_proto->ParseFromString(proto_arg.protobuf_data())) {
      LOG(FATAL) << "Unable to deserialized proto for "
                 << proto_arg.full_name();
    }
    protos_to_be_destroyed_.push_back({src, deserialized_proto});
    return &protos_to_be_destroyed_.back().second;
  }

  // Use list instead of vector to preserve references even with modifications.
  // Contains pairs of lenval message pointer -> deserialized message
  // so that we can serialize the argument again after the function call.
  std::list<std::pair<LenValStruct*, google::protobuf::MessageLite*>>
      protos_to_be_destroyed_;
  ffi_type* ret_type_;
  ffi_type* arg_types_[FuncCall::kArgsMax];
  const void* arg_values_[FuncCall::kArgsMax];
};

}  // namespace

namespace client {

// Error codes in the client code:
enum class Error : uintptr_t {
  kUnset = 0,
  kDlOpen,
  kDlSym,
  kCall,
};

// Handles requests to make function calls.
void HandleCallMsg(const FuncCall& call, FuncRet* ret) {
  VLOG(1) << "HandleMsgCall, func: '" << call.func
          << "', # of args: " << call.argc;

  ret->ret_type = call.ret_type;

  void* handle = dlopen(nullptr, RTLD_NOW);
  if (handle == nullptr) {
    LOG(ERROR) << "dlopen(nullptr, RTLD_NOW)";
    ret->success = false;
    ret->int_val = static_cast<uintptr_t>(Error::kDlOpen);
    return;
  }

  auto f = dlsym(handle, call.func);
  if (f == nullptr) {
    LOG(ERROR) << "Function '" << call.func << "' not found";
    ret->success = false;
    ret->int_val = static_cast<uintptr_t>(Error::kDlSym);
    return;
  }
  FunctionCallPreparer arg_prep(call);
  ffi_cif cif;
  if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, call.argc, arg_prep.ret_type(),
                   arg_prep.arg_types()) != FFI_OK) {
    ret->success = false;
    ret->int_val = static_cast<uintptr_t>(Error::kCall);
    return;
  }

  if (ret->ret_type == v::Type::kFloat) {
    ffi_call(&cif, FFI_FN(f), &ret->float_val, arg_prep.arg_values());
  } else {
    ffi_call(&cif, FFI_FN(f), &ret->int_val, arg_prep.arg_values());
  }

  ret->success = true;
}

// Handles requests to allocate memory inside the sandboxee.
void HandleAllocMsg(const size_t size, FuncRet* ret) {
  VLOG(1) << "HandleAllocMsg: size=" << size;

  const void* allocated = malloc(size);
  // Memory is copied to the pointer using an API that the memory sanitizer
  // is blind to (process_vm_writev). Mark the memory as initialized here, so
  // that the sandboxed code can still be tested using MSAN.
  ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(allocated, size);

  ret->ret_type = v::Type::kPointer;
  ret->int_val = reinterpret_cast<uintptr_t>(allocated);
  ret->success = true;
}

// Like HandleAllocMsg(), but handles requests to reallocate memory.
void HandleReallocMsg(uintptr_t ptr, size_t size, FuncRet* ret) {
  VLOG(1) << "HandleReallocMsg(" << absl::StrCat(absl::Hex(ptr)) << ", " << size
          << ")";

  const void* reallocated = realloc(reinterpret_cast<void*>(ptr), size);
  // Memory is copied to the pointer using an API that the memory sanitizer
  // is blind to (process_vm_writev). Mark the memory as initialized here, so
  // that the sandboxed code can still be tested using MSAN.
  ABSL_ANNOTATE_MEMORY_IS_INITIALIZED(reallocated, size);

  ret->ret_type = v::Type::kPointer;
  ret->int_val = reinterpret_cast<uintptr_t>(reallocated);
  ret->success = true;
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
