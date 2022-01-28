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

#ifndef SANDBOXED_API_CALL_H_
#define SANDBOXED_API_CALL_H_

#include <cstdint>

#include "sandboxed_api/var_type.h"

namespace sapi {
namespace comms {

struct ReallocRequest {
  uintptr_t old_addr;
  size_t size;
};

// Types of TAGs used with Comms channel.
// Call:
constexpr uint32_t kMsgCall = 0x101;
constexpr uint32_t kMsgAllocate = 0x102;
constexpr uint32_t kMsgFree = 0x103;
constexpr uint32_t kMsgExit = 0x104;
constexpr uint32_t kMsgSymbol = 0x105;
constexpr uint32_t kMsgSendFd = 0x106;
constexpr uint32_t kMsgRecvFd = 0x107;
constexpr uint32_t kMsgClose = 0x108;
constexpr uint32_t kMsgReallocate = 0x109;
constexpr uint32_t kMsgStrlen = 0x10A;
// Return:
constexpr uint32_t kMsgReturn = 0x201;

}  // namespace comms

struct FuncCall {
  // Used with HandleCallMsg:
  enum {
    kFuncNameMax = 128,
    kArgsMax = 12,
  };

  // Function to be called.
  char func[kFuncNameMax];
  // Return type.
  v::Type ret_type;
  // Size of the return value (in bytes).
  size_t ret_size;
  // Number of input arguments.
  size_t argc;
  // Types of the input arguments.
  v::Type arg_type[kArgsMax];
  // Size (in bytes) of input arguments.
  size_t arg_size[kArgsMax];
  // Arguments to the call.
  union {
    uintptr_t arg_int;
    long double arg_float;
  } args[kArgsMax];
  // Auxiliary type:
  //  For pointers: type of the data it points to,
  //  For others: unspecified.
  v::Type aux_type[kArgsMax];
  // Size of the auxiliary data (e.g. a structure the pointer points to).
  size_t aux_size[kArgsMax];
};

struct FuncRet {
  // Return type:
  v::Type ret_type;
  // Return value.
  union {
    uintptr_t int_val;
    long double float_val;
  };
  // Status of the operation: success/failure.
  bool success;
};

}  // namespace sapi

#endif  // SANDBOXED_API_CALL_H_
