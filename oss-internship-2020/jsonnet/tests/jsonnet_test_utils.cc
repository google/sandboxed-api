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

// Prepare what is needed to perform a test.
void JsonnetTestHelper::TestSetUp() {
  // Set up sandbox and api.
  sandbox = std::make_unique<JsonnetBaseSandbox>();
  ASSERT_THAT(sandbox->Init(), sapi::IsOk());
  api = std::make_unique<JsonnetApi>(sandbox.get());

  // Initialize library's main structure.
  SAPI_ASSERT_OK_AND_ASSIGN(JsonnetVm * vm_ptr, api->c_jsonnet_make());
  vm = std::make_unique<sapi::v::RemotePtr>(vm_ptr);

  return;
}

// Clean up after a test.
void JsonnetTestHelper::TestTearDown() {
  SAPI_ASSERT_OK_AND_ASSIGN(char* result,
                            api->c_jsonnet_realloc(vm.get(), output.get(), 0));
  ASSERT_THAT(api->c_jsonnet_destroy(vm.get()), sapi::IsOk());
  ASSERT_THAT(api->c_free_input(input.get()), sapi::IsOk());

  return;
}

// Read input from file.
void JsonnetTestHelper::Read_input(char* filename) {
  std::string in_file_in_sandboxee(std::string("/input/") +
                                   basename(&filename[0]));
  input_filename_in_sandboxee = std::move(in_file_in_sandboxee);
  sapi::v::ConstCStr in_file_var(input_filename_in_sandboxee.c_str());

  SAPI_ASSERT_OK_AND_ASSIGN(char* input_ptr,
                            api->c_read_input(0, in_file_var.PtrBefore()));
  input = std::make_unique<sapi::v::RemotePtr>(input_ptr);

  return;
}

// Evaluate jsonnet code.
void JsonnetTestHelper::Evaluate_jsonnet_code(char* filename, Evaluation type) {
  sapi::v::ConstCStr in_file_var(input_filename_in_sandboxee.c_str());
  sapi::v::Int error;
  char* output_ptr;

  switch (type) {
    case BASE: {
      SAPI_ASSERT_OK_AND_ASSIGN(
          output_ptr,
          api->c_jsonnet_evaluate_snippet(vm.get(), in_file_var.PtrBefore(),
                                          input.get(), error.PtrAfter()));
      break;
    }

    case MULTIPLE_FILES: {
      SAPI_ASSERT_OK_AND_ASSIGN(
          output_ptr, api->c_jsonnet_evaluate_snippet_multi(
                          vm.get(), in_file_var.PtrBefore(), input.get(),
                          error.PtrAfter()));
      break;
    }

    case YAML_STREAM: {
      SAPI_ASSERT_OK_AND_ASSIGN(
          output_ptr, api->c_jsonnet_evaluate_snippet_stream(
                          vm.get(), in_file_var.PtrBefore(), input.get(),
                          error.PtrAfter()));
      break;
    }
  }

  ASSERT_THAT(error.GetValue(), testing::Eq(0));
  output = std::make_unique<sapi::v::RemotePtr>(output_ptr);

  return;
}

// Write output to file.
void JsonnetTestHelper::Write_output(char* filename_or_directory,
                                     Evaluation type) {
  bool success;

  switch (type) {
    case BASE: {
      std::string out_file_in_sandboxee(std::string("/output/") +
                                        basename(&filename_or_directory[0]));
      sapi::v::ConstCStr out_file_var(out_file_in_sandboxee.c_str());

      SAPI_ASSERT_OK_AND_ASSIGN(
          success,
          api->c_write_output_file(output.get(), out_file_var.PtrBefore()));
      break;
    }
    case MULTIPLE_FILES: {
      std::string out_file_in_sandboxee(std::string("/output/"));
      sapi::v::ConstCStr out_file_var(out_file_in_sandboxee.c_str());
      SAPI_ASSERT_OK_AND_ASSIGN(
          success, api->c_write_multi_output_files(output.get(),
                                                   out_file_var.PtrBefore()));
      break;
    }

    case YAML_STREAM: {
      std::string out_file_in_sandboxee(std::string("/output/") +
                                        basename(&out_file[0]));
      sapi::v::ConstCStr out_file_var(out_file_in_sandboxee.c_str());
      SAPI_ASSERT_OK_AND_ASSIGN(
          success,
          api->c_write_output_stream(output.get(), out_file_var.PtrBefore()));
      break;
    }
  }

  ASSERT_THAT(success, testing::Eq(true));

  return;
}
