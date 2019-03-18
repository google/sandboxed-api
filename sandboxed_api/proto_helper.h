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

// Utility functions for protobuf handling.

#ifndef SANDBOXED_API_PROTO_HELPER_H_
#define SANDBOXED_API_PROTO_HELPER_H_

#include <cinttypes>
#include <vector>

#include <glog/logging.h>
#include "sandboxed_api/proto_arg.pb.h"

namespace sapi {

template <typename T>
std::vector<uint8_t> SerializeProto(const T& proto) {
  // Wrap protobuf in a envelope so that we know the name of the protobuf
  // structure when deserializing in the sandboxee.
  ProtoArg proto_arg;
  proto_arg.set_protobuf_data(proto.SerializeAsString());
  proto_arg.set_full_name(proto.GetDescriptor()->full_name());
  std::vector<uint8_t> serialized_proto(proto_arg.ByteSizeLong());

  if (!proto_arg.SerializeToArray(serialized_proto.data(),
                                  serialized_proto.size())) {
    LOG(ERROR) << "Unable to serialize array";
  }

  return serialized_proto;
}

template <typename T>
bool DeserializeProto(T* result, const char* data, size_t len) {
  ProtoArg envelope;
  if (!envelope.ParseFromArray(data, len)) {
    LOG(ERROR) << "Unable to deserialize envelope";
    return false;
  }

  auto pb_data = envelope.protobuf_data();
  return result->ParseFromArray(pb_data.c_str(), pb_data.size());
}

}  // namespace sapi

#endif  // SANDBOXED_API_PROTO_HELPER_H_
