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

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <streambuf>
#include <string>

#include "gtest/gtest.h"
#include "jsonnet_base_sandbox.h"  // NOLINT(build/include)
#include "jsonnet_sapi.sapi.h"
#include "sandboxed_api/util/flag.h"
#include "sandboxed_api/util/status_matchers.h"

class JsonnetTestHelper {
 protected:
  enum Evaluation { BASE, MULTIPLE_FILES, YAML_STREAM };

  void TestSetUp();
  void TestTearDown();

  void Read_input(char* filename);
  void Evaluate_jsonnet_code(char* filename, Evaluation type,
                             bool expected_correct);
  void Write_output(char* filename_or_directory, Evaluation type);
  std::string Read_output(char* filename);

  std::unique_ptr<JsonnetBaseSandbox> sandbox;
  std::unique_ptr<JsonnetApi> api;
  std::unique_ptr<sapi::v::RemotePtr> input;
  std::unique_ptr<sapi::v::RemotePtr> output;
  std::unique_ptr<sapi::v::RemotePtr> vm;

  std::string input_filename_in_sandboxee;
  bool if_jsonnet_vm_was_used;
  bool if_input_was_read;
};
