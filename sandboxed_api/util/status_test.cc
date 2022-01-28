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

#include "absl/status/status.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/util/status.h"

using ::testing::Eq;
using ::testing::StrEq;

namespace sapi {
namespace {

StatusProto OkStatusProto() {
  StatusProto proto;
  proto.set_code(static_cast<int>(absl::StatusCode::kOk));
  return proto;
}

StatusProto InvalidArgumentStatusProto(absl::string_view msg) {
  StatusProto proto;
  proto.set_code(static_cast<int>(absl::StatusCode::kInvalidArgument));
  proto.set_message(std::string(msg));
  return proto;
}

TEST(StatusTest, SaveOkStatusProto) {
  StatusProto proto;
  SaveStatusToProto(absl::OkStatus(), &proto);
  const auto ok_proto = OkStatusProto();
  EXPECT_THAT(proto.code(), Eq(ok_proto.code()));
  EXPECT_THAT(proto.message(), StrEq(ok_proto.message()));
}

TEST(StatusTest, SaveStatusWithMessage) {
  constexpr char kErrorMessage[] = "Bad foo argument";
  absl::Status status(absl::StatusCode::kInvalidArgument, kErrorMessage);
  StatusProto proto;
  SaveStatusToProto(status, &proto);
  const auto invalid_proto = InvalidArgumentStatusProto(kErrorMessage);
  EXPECT_THAT(proto.code(), Eq(invalid_proto.code()));
  EXPECT_THAT(proto.message(), StrEq(invalid_proto.message()));
}

}  // namespace
}  // namespace sapi
