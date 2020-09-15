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

#include "jsonnet_base_sandbox.h"
#include "jsonnet_sapi.sapi.h"
#include "gtest/gtest.h"
#include "sandboxed_api/util/flag.h"
#include "sandboxed_api/util/status_matchers.h"

class JsonnetTestHelper {
  protected:
    enum Evaluation { BASE, MULTIPLE_FILES, YAML_STREAM };

    void JsonnetTestSetUp();
    void JsonnetTestTearDown();

    char* Read_input(const char* filename);
    char* Evaluate_jsonnet_code(struct JsonnetVm* vm, const char* filename, Evaluation type);
    bool Write_output(struct JsonnetVm* vm, char* output, char* filename_or_directory, Evaluation type);

    std::unique_ptr<JsonnetBaseSandbox> sandbox;
    std::unique_ptr<JsonnetApi> api;
    std::unique_ptr<sapi::v::RemotePtr> input;
    
    JsonnetVm* vm;

};