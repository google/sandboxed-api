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

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "absl/log/check.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/function_call_helper.h"
#include "sandboxed_api/var_type.h"

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

// Interface to prepare the arguments for a function call using libffi.
class LibFFIFunctionCallPreparer : public FunctionCallPreparer {
 public:
  explicit LibFFIFunctionCallPreparer(const FuncCall& call)
      : FunctionCallPreparer(call) {
    for (int i = 0; i < call.argc; ++i) {
      arg_types_[i] = GetFFIType(call.arg_size[i], call.arg_type[i]);
    }
    ret_type_ = GetFFIType(call.ret_size, call.ret_type);
  }

  ffi_type* ret_type() const { return ret_type_; }
  ffi_type** arg_types() const { return const_cast<ffi_type**>(arg_types_); }

 private:
  ffi_type* ret_type_;
  ffi_type* arg_types_[FuncCall::kArgsMax];
};

}  // namespace

namespace client {

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
  LibFFIFunctionCallPreparer arg_prep(call);
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

}  // namespace client
}  // namespace sapi
