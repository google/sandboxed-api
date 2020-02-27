// Copyright 2019 Google LLC
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

// Utility functions for protobuf handling.

#ifndef SANDBOXED_API_PROTO_HELPER_H_
#define SANDBOXED_API_PROTO_HELPER_H_

#include <cinttypes>
#include <type_traits>
#include <vector>

#include "absl/status/status.h"
#include "sandboxed_api/proto_arg.pb.h"
#include "sandboxed_api/util/statusor.h"

namespace sapi {

template <typename T>
sapi::StatusOr<std::vector<uint8_t>> SerializeProto(const T& proto) {
  static_assert(std::is_base_of<google::protobuf::Message, T>::value,
                "Template argument must be a proto message");
  // Wrap protobuf in a envelope so that we know the name of the protobuf
  // structure when deserializing in the sandboxee.
  ProtoArg proto_arg;
  proto_arg.set_protobuf_data(proto.SerializeAsString());
  proto_arg.set_full_name(proto.GetDescriptor()->full_name());

  std::vector<uint8_t> serialized_proto(proto_arg.ByteSizeLong());
  if (!proto_arg.SerializeToArray(serialized_proto.data(),
                                  serialized_proto.size())) {
    return absl::InternalError("Unable to serialize proto to array");
  }
  return serialized_proto;
}

template <typename T>
sapi::StatusOr<T> DeserializeProto(const char* data, size_t len) {
  static_assert(std::is_base_of<google::protobuf::Message, T>::value,
                "Template argument must be a proto message");
  ProtoArg envelope;
  if (!envelope.ParseFromArray(data, len)) {
    return absl::InternalError("Unable to parse proto from array");
  }

  auto pb_data = envelope.protobuf_data();
  T result;
  if (!result.ParseFromArray(pb_data.data(), pb_data.size())) {
    return absl::InternalError("Unable to parse proto from envelope data");
  }
  return result;
}

}  // namespace sapi

#endif  // SANDBOXED_API_PROTO_HELPER_H_
