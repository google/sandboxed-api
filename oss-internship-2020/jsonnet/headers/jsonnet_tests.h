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
#include "sandboxed_api/sandbox2/util/path.h"

class JsonnetTestHelper {
 protected:
  enum Evaluation { kBase, kMultipleFiles, kYamlStream };

  void TestSetUp();
  void TestTearDown();

  void Read_input(const char* filename);
  void Evaluate_jsonnet_code(Evaluation type,
                             bool expected_correct);
  void Write_output(const char* filename_or_directory, Evaluation type);
  std::string Read_output(const char* filename);

  std::unique_ptr<JsonnetBaseSandbox> sandbox_;
  std::unique_ptr<JsonnetApi> api_;
  std::unique_ptr<sapi::v::RemotePtr> input_;
  std::unique_ptr<sapi::v::RemotePtr> output_;
  std::unique_ptr<sapi::v::RemotePtr> vm_;

  std::string input_filename_in_sandboxee_;
  bool if_jsonnet_vm_was_used_;
  bool if_input_was_read_;
};
