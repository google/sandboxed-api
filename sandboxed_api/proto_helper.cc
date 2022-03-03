// Copyright 2022 Google LLC
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

#include "sandboxed_api/proto_helper.h"

#include "absl/status/status.h"

namespace sapi {

namespace internal {

absl::Status DeserializeProto(const char* data, size_t len,
                              google::protobuf::MessageLite& output) {
  ProtoArg envelope;
  if (!envelope.ParseFromArray(data, len)) {
    return absl::InternalError("Unable to parse proto from array");
  }

  auto pb_data = envelope.protobuf_data();
  if (!output.ParseFromArray(pb_data.data(), pb_data.size())) {
    return absl::InternalError("Unable to parse proto from envelope data");
  }
  return absl::OkStatus();
}

}  // namespace internal

absl::StatusOr<std::vector<uint8_t>> SerializeProto(
    const google::protobuf::MessageLite& proto) {
  // Wrap protobuf in a envelope so that we know the name of the protobuf
  // structure when deserializing in the sandboxee.
  ProtoArg proto_arg;
  proto_arg.set_protobuf_data(proto.SerializeAsString());
  proto_arg.set_full_name(proto.GetTypeName());
  std::vector<uint8_t> serialized_proto(proto_arg.ByteSizeLong());
  if (!proto_arg.SerializeToArray(serialized_proto.data(),
                                  serialized_proto.size())) {
    return absl::InternalError("Unable to serialize proto to array");
  }
  return serialized_proto;
}

}  // namespace sapi
