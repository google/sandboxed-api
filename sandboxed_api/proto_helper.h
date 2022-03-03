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

// Utility functions for protobuf handling.

#ifndef SANDBOXED_API_PROTO_HELPER_H_
#define SANDBOXED_API_PROTO_HELPER_H_

#include <cinttypes>
#include <type_traits>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "sandboxed_api/proto_arg.pb.h"
#include "sandboxed_api/util/status_macros.h"

namespace sapi {

namespace internal {

absl::Status DeserializeProto(const char* data, size_t len,
                              google::protobuf::MessageLite& output);

}  // namespace internal

absl::StatusOr<std::vector<uint8_t>> SerializeProto(
    const google::protobuf::MessageLite& proto);

template <typename T>
absl::StatusOr<T> DeserializeProto(const char* data, size_t len) {
  static_assert(std::is_base_of<google::protobuf::MessageLite, T>::value,
                "Template argument must be a proto message");
  T result;
  SAPI_RETURN_IF_ERROR(
      internal::DeserializeProto(data, len, /*output=*/result));
  return result;
}

}  // namespace sapi

#endif  // SANDBOXED_API_PROTO_HELPER_H_
