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

#include "jsonnet_tests.h"  // NOLINT(build/include)

namespace {

class JsonnetTest : public JsonnetTestHelper, public testing::Test {
 protected:
  void SetUp() override { TestSetUp(); }
  void TearDown() override { TestTearDown(); }
};

// Basic test
TEST_F(JsonnetTest, SetUp_TearDown) {
  ASSERT_FALSE(jsonnet_vm_was_used_);
  ASSERT_FALSE(input_was_read_);
}

// One file evaluation to one file
TEST_F(JsonnetTest, One_file_no_dependencies) {
  constexpr char kInputFile[] = "arith.jsonnet";
  constexpr char kOutputFile[] = "arith_output";
  constexpr char kOutputToRead[] = "tests_output/arith_output";
  constexpr char kOutputToExpect[] = "tests_expected_output/arith.golden";

  ReadInput(kInputFile);
  EvaluateJsonnetCode(kBase, true);
  WriteOutput(kOutputFile, kBase);

  std::string produced_output = ReadOutput(kOutputToRead);
  std::string expected_output = ReadOutput(kOutputToExpect);

  ASSERT_STREQ(produced_output.c_str(), expected_output.c_str());
}

// One file evaluating to one file, dependent on some other files
TEST_F(JsonnetTest, One_file_some_dependencies) {
  constexpr char kInputFile[] = "negroni.jsonnet";
  constexpr char kOutputFile[] = "negroni_output";
  constexpr char kOutputToRead[] = "tests_output/negroni_output";
  constexpr char kOutputToExpect[] = "tests_expected_output/negroni.golden";

  ReadInput(kInputFile);
  EvaluateJsonnetCode(kBase, true);
  WriteOutput(kOutputFile, kBase);

  const std::string produced_output = ReadOutput(kOutputToRead);
  const std::string expected_output = ReadOutput(kOutputToExpect);

  ASSERT_STREQ(produced_output.c_str(), expected_output.c_str());
}

// One file evaluating to two files
TEST_F(JsonnetTest, Multiple_files) {
  constexpr char kInputFile[] = "multiple_files_example.jsonnet";
  constexpr char kOutputFile[] = "";
  constexpr char kOutputToRead1[] = "tests_output/first_file.json";
  constexpr char kOutputToRead2[] = "tests_output/second_file.json";
  constexpr char kOutputToExpect1[] = "tests_expected_output/first_file.json";
  constexpr char kOutputToExpect2[] = "tests_expected_output/second_file.json";

  ReadInput(kInputFile);
  EvaluateJsonnetCode(kMultipleFiles, true);
  WriteOutput(kOutputFile, kMultipleFiles);

  const std::string produced_output_1 = ReadOutput(kOutputToRead1);
  const std::string produced_output_2 = ReadOutput(kOutputToRead2);
  const std::string expected_output_1 = ReadOutput(kOutputToExpect1);
  const std::string expected_output_2 = ReadOutput(kOutputToExpect2);

  ASSERT_STREQ(produced_output_1.c_str(), expected_output_1.c_str());
  ASSERT_STREQ(produced_output_2.c_str(), expected_output_2.c_str());
}

// One file evaluating to yaml stream format
TEST_F(JsonnetTest, Yaml_stream) {
  constexpr char kInputFile[] = "yaml_stream_example.jsonnet";
  constexpr char kOutputFile[] = "yaml_stream_example.yaml";
  constexpr char kOutputToRead[] = "tests_output/yaml_stream_example.yaml";
  constexpr char kOutputToExpect[] =
      "tests_expected_output/yaml_stream_example.yaml";

  ReadInput(kInputFile);
  EvaluateJsonnetCode(kYamlStream, true);
  WriteOutput(kOutputFile, kYamlStream);

  const std::string produced_output = ReadOutput(kOutputToRead);
  const std::string expected_output = ReadOutput(kOutputToExpect);

  ASSERT_STREQ(produced_output.c_str(), expected_output.c_str());
}

// One file depended on some other files not accessible by the sandbox
TEST_F(JsonnetTest, Bad_evaluation) {
  constexpr char kInputFile[] = "imports.jsonnet";

  ReadInput(kInputFile);
  EvaluateJsonnetCode(kBase, false);
}

}  // namespace
