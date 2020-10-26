// Copyright 2020 Google LLC
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

#include "sandboxed_api/tools/clang_generator/emitter.h"

#include <initializer_list>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/testing.h"
#include "sandboxed_api/tools/clang_generator/frontend_action_test_util.h"
#include "sandboxed_api/tools/clang_generator/generator.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sapi {
namespace {

using ::testing::MatchesRegex;
using ::testing::SizeIs;
using ::testing::StrEq;
using ::testing::StrNe;

class EmitterForTesting : public Emitter {
 public:
  using Emitter::functions_;
};

class EmitterTest : public FrontendActionTest {};

TEST_F(EmitterTest, BasicFunctionality) {
  GeneratorOptions options;
  options.out_file = "input.h";
  options.set_function_names<std::initializer_list<std::string>>(
      {"ExposedFunction"});

  EmitterForTesting emitter;
  RunFrontendAction(R"(extern "C" void ExposedFunction() {})",
                    absl::make_unique<GeneratorAction>(emitter, options));

  EXPECT_THAT(emitter.functions_, SizeIs(1));

  absl::StatusOr<std::string> header = emitter.EmitHeader(options);
  EXPECT_THAT(header, IsOk());
}

TEST(IncludeGuard, CreatesRandomizedGuardForEmptyFilename) {
  // Copybara will transform the string. This is intentional.
  constexpr absl::string_view kGeneratedHeaderPrefix =
      "SANDBOXED_API_GENERATED_HEADER_";

  const std::string include_guard = GetIncludeGuard("");
  EXPECT_THAT(include_guard, MatchesRegex(absl::StrCat(kGeneratedHeaderPrefix,
                                                       R"([0-9A-F]+_)")));

  EXPECT_THAT(GetIncludeGuard(""), StrNe(include_guard));
}

TEST(IncludeGuard, BasicFunctionality) {
  EXPECT_THAT(GetIncludeGuard("boost/graph/compressed_sparse_row_graph.hpp"),
              StrEq("BOOST_GRAPH_COMPRESSED_SPARSE_ROW_GRAPH_HPP_"));

  // "SAPI_" prefix is there to avoid generating guards starting with "_"
  EXPECT_THAT(GetIncludeGuard("/usr/include/unistd.h"),
              StrEq("SAPI_USR_INCLUDE_UNISTD_H_"));
}

TEST(IncludeGuard, AvoidReservedIdentifiers) {
  EXPECT_THAT(GetIncludeGuard("9p.h"), StrEq("SAPI_9P_H_"));
  EXPECT_THAT(GetIncludeGuard("double__under.h"), StrEq("DOUBLE_UNDER_H_"));
  EXPECT_THAT(GetIncludeGuard("_single.h"), StrEq("SAPI_SINGLE_H_"));
  EXPECT_THAT(GetIncludeGuard("__double.h"), StrEq("SAPI_DOUBLE_H_"));
}

}  // namespace
}  // namespace sapi
