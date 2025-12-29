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

#ifndef SANDBOXED_API_FUNCTION_CALL_HELPER_H_
#define SANDBOXED_API_FUNCTION_CALL_HELPER_H_

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <list>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/lenval_core.h"
#include "sandboxed_api/util/proto_arg.pb.h"
#include "sandboxed_api/util/proto_helper.h"
#include "sandboxed_api/var_type.h"

namespace sapi {

// Provides an interface to prepare the arguments for a function call.
// In case of protobuf arguments, the class allocates and manages
// memory for the deserialized protobuf.
class FunctionCallPreparer {
 public:
  explicit FunctionCallPreparer(const FuncCall& call) : argc_(call.argc) {
    CHECK(call.argc <= FuncCall::kArgsMax)
        << "Number of arguments of a sandbox call exceeds limits.";
    for (size_t i = 0; i < argc_; ++i) {
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
      arg_type_[i] = call.arg_type[i];
    }
  }

  virtual ~FunctionCallPreparer() {
    for (auto& [lvs, proto] : protos_to_be_destroyed_) {
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

  void** arg_values() const { return const_cast<void**>(arg_values_); }
  size_t arg_count() const { return argc_; }

  template <typename T>
  T GetArg(size_t idx) const {
    T ret;
    memcpy(&ret, arg_values()[idx], sizeof(T));
    return ret;
  }

  template <typename T>
  bool HasCompatibleArg(size_t idx) const {
    if constexpr (std::is_pointer_v<T>) {
      return arg_type_[idx] == v::Type::kPointer;
    } else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
      return arg_type_[idx] == v::Type::kInt;
    } else if constexpr (std::is_floating_point_v<T>) {
      return arg_type_[idx] == v::Type::kFloat;
    }
    LOG(FATAL) << "Unsupported type: " << arg_type_[idx];
  }

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
  const void* arg_values_[FuncCall::kArgsMax];
  v::Type arg_type_[FuncCall::kArgsMax];
  size_t argc_;
};

template <typename T>
FuncRet ToFuncRet(T val) {
  FuncRet ret = {.success = true};
  if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
    ret.ret_type = v::Type::kInt;
  } else if constexpr (std::is_floating_point_v<T>) {
    ret.ret_type = v::Type::kFloat;
  } else if constexpr (std::is_pointer_v<T>) {
    ret.ret_type = v::Type::kPointer;
  }
  if constexpr (std::is_floating_point_v<T>) {
    static_assert(sizeof(T) <= sizeof(ret.float_val), "float_val is too small");
    memcpy(&ret.float_val, &val, sizeof(T));
  } else {
    static_assert(sizeof(T) <= sizeof(ret.int_val), "int_val is too small");
    memcpy(&ret.int_val, &val, sizeof(T));
  }
  return ret;
}

}  // namespace sapi

#endif  // SANDBOXED_API_FUNCTION_CALL_HELPER_H_
