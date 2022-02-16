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

#include "sandboxed_api/util/status.h"

#include "absl/status/status.h"

namespace sapi {

void SaveStatusToProto(const absl::Status& status, StatusProto* out) {
  out->set_code(status.raw_code());
  out->set_message(std::string(status.message()));
  auto* payloads = out->mutable_payloads();
  status.ForEachPayload(
      [payloads](absl::string_view type_key, const absl::Cord& payload) {
        (*payloads)[std::string(type_key)] = static_cast<std::string>(payload);
      });
}

absl::Status MakeStatusFromProto(const StatusProto& proto) {
  absl::Status status(static_cast<absl::StatusCode>(proto.code()),
                      proto.message());
  // Note: Using C++17 structured bindings instead of `entry` crashes Clang 6.0
  // on Ubuntu 18.04 (bionic).
  for (const auto& entry : proto.payloads()) {
    status.SetPayload(/*type_url=*/entry.first,
                      /*payload=*/absl::Cord(entry.second));
  }
  return status;
}

}  // namespace sapi
