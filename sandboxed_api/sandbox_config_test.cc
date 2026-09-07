// Copyright 2026 Google LLC
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

#include "sandboxed_api/sandbox_config.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/linked_hash_map.h"

namespace sapi {
namespace {

using ::testing::ElementsAre;
using ::testing::Optional;
using ::testing::Pair;

TEST(SandboxConfigTest, LinkedHashMapIterationOrder) {
  auto config = SandboxConfig::DefaultConfig();
  config.command_line_flags = []() mutable {
    absl::linked_hash_map<std::string, std::string> flags;
    flags["logtostderr"] = "false";
    flags["stderrthreshold"] = "2";
    flags["alsologtostderr"] = "true";
    flags["undefok"] = "nonexistent_flag1,nonexistent_flag2";
    return flags;
  }();
  EXPECT_THAT(config.command_line_flags,
              Optional(ElementsAre(
                  Pair("logtostderr", "false"), Pair("stderrthreshold", "2"),
                  Pair("alsologtostderr", "true"),
                  Pair("undefok", "nonexistent_flag1,nonexistent_flag2"))));
}

}  // namespace
}  // namespace sapi
