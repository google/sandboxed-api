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

#include "jsonnet_tests.h"

class JsonnetTest : public JsonnetTestHelper, public testing::Test {
 protected:
  void SetUp() override { TestSetUp(); }
  void TearDown() override { TestTearDown(); }
};

// Basic test
TEST_F(JsonnetTest, SetUp_TearDown) {
  ASSERT_FALSE(if_jsonnet_vm_was_used);
  ASSERT_FALSE(if_input_was_read);
}

// One file evaluation to one file
TEST_F(JsonnetTest, One_file_no_dependencies) {
  char input_file[] = "arith.jsonnet";
  char output_file[] = "arith_output";
  char output_to_read[] = "tests_output/arith_output";
  char output_to_expect[] = "tests_expected_output/arith.golden";

  Read_input(input_file);
  Evaluate_jsonnet_code(input_file, BASE, true);
  Write_output(output_file, BASE);

  std::string produced_output = Read_output(output_to_read);
  std::string expected_output = Read_output(output_to_expect);

  ASSERT_STREQ(produced_output.c_str(), expected_output.c_str());
}

// One file evaluating to one file, dependent on some other files
TEST_F(JsonnetTest, One_file_some_dependencies) {
  char input_file[] = "negroni.jsonnet";
  char output_file[] = "negroni_output";
  char output_to_read[] = "tests_output/negroni_output";
  char output_to_expect[] = "tests_expected_output/negroni.golden";

  Read_input(input_file);
  Evaluate_jsonnet_code(input_file, BASE, true);
  Write_output(output_file, BASE);

  std::string produced_output = Read_output(output_to_read);
  std::string expected_output = Read_output(output_to_expect);

  ASSERT_STREQ(produced_output.c_str(), expected_output.c_str());
}

// One file evaluating to two files
TEST_F(JsonnetTest, Multiple_files) {
  char input_file[] = "multiple_files_example.jsonnet";
  char output_file[] = "";
  char output_to_read_1[] = "tests_output/first_file.json";
  char output_to_read_2[] = "tests_output/second_file.json";
  char output_to_expect_1[] = "tests_expected_output/first_file.json";
  char output_to_expect_2[] = "tests_expected_output/second_file.json";

  Read_input(input_file);
  Evaluate_jsonnet_code(input_file, MULTIPLE_FILES, true);
  Write_output(output_file, MULTIPLE_FILES);

  std::string produced_output_1 = Read_output(output_to_read_1);
  std::string produced_output_2 = Read_output(output_to_read_2);
  std::string expected_output_1 = Read_output(output_to_expect_1);
  std::string expected_output_2 = Read_output(output_to_expect_2);

  ASSERT_STREQ(produced_output_1.c_str(), expected_output_1.c_str());
  ASSERT_STREQ(produced_output_2.c_str(), expected_output_2.c_str());
}

// One file evaluating to yaml stream format
TEST_F(JsonnetTest, Yaml_stream) {
  char input_file[] = "yaml_stream_example.jsonnet";
  char output_file[] = "yaml_stream_example.yaml";
  char output_to_read[] = "tests_output/yaml_stream_example.yaml";
  char output_to_expect[] = "tests_expected_output/yaml_stream_example.yaml";

  Read_input(input_file);
  Evaluate_jsonnet_code(input_file, YAML_STREAM, true);
  Write_output(output_file, YAML_STREAM);

  std::string produced_output = Read_output(output_to_read);
  std::string expected_output = Read_output(output_to_expect);

  ASSERT_STREQ(produced_output.c_str(), expected_output.c_str());
}

// One file depended on some other files not accessible by the sandbox
TEST_F(JsonnetTest, Bad_evaluation) {
  char input_file[] = "imports.jsonnet";

  Read_input(input_file);
  Evaluate_jsonnet_code(input_file, BASE, false);
}
