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

#ifndef SANDBOXED_API_VAR_LENVAL_H_
#define SANDBOXED_API_VAR_LENVAL_H_

#include <sys/uio.h>

#include <memory>

#include "absl/base/macros.h"
#include "sandboxed_api/lenval_core.h"
#include "sandboxed_api/var_abstract.h"
#include "sandboxed_api/var_array.h"
#include "sandboxed_api/var_ptr.h"
#include "sandboxed_api/var_struct.h"

namespace sapi::v {

template <class T>
class Proto;

// Length + value container. Represents a pointer to a LenValStruct inside the
// sandboxee which allows the bidirectional synchronization data structures with
// changing lengths (e.g. protobuf structures). You probably want to directly
// use protobufs as they are easier to handle.
class LenVal : public Var {
 public:
  explicit LenVal(const char* data, uint64_t size)
      : array_(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(data)),
               size),
        struct_(size, nullptr) {}

  explicit LenVal(const std::vector<uint8_t>& data)
      : array_(data.size()), struct_(data.size(), nullptr) {
    memcpy(array_.GetData(), data.data(), data.size());
  }

  explicit LenVal(size_t size) : array_(size), struct_(size, nullptr) {}

  Type GetType() const final { return Type::kLenVal; }
  std::string GetTypeString() const final { return "LengthValue"; }
  std::string ToString() const final { return "LenVal"; }

  absl::Status ResizeData(RPCChannel* rpc_channel, size_t size);
  size_t GetDataSize() const { return struct_.data().size; }
  uint8_t* GetData() const { return array_.GetData(); }
  void* GetRemote() const final { return struct_.GetRemote(); }

 protected:
  size_t GetSize() const final { return 0; }

  absl::Status Allocate(RPCChannel* rpc_channel, bool automatic_free) override;
  absl::Status Free(RPCChannel* rpc_channel) override;
  absl::Status TransferToSandboxee(RPCChannel* rpc_channel, pid_t pid) override;
  absl::Status TransferFromSandboxee(RPCChannel* rpc_channel,
                                     pid_t pid) override;

  Array<uint8_t> array_;
  Struct<LenValStruct> struct_;

  template <class T>
  friend class Proto;
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_LENVAL_H_
