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

  // Get paths to where input and output is stored.
  char buffer[256];
  int error = readlink("/proc/self/exe", buffer, 256);
  std::filesystem::path binary_path = dirname(buffer);
  std::filesystem::path input_path = binary_path / "tests_input" / "dummy_input";
  std::filesystem::path output_path = binary_path / "tests_output" / "dummy_output";

  // Set up sandbox and api.
  sandbox = std::make_unique<JsonnetBaseSandbox>(input_path.string(), output_path.string());
  ASSERT_THAT(sandbox->Init(), sapi::IsOk());
  api = std::make_unique<JsonnetApi>(sandbox.get());

  // Initialize library's main structure.
  SAPI_ASSERT_OK_AND_ASSIGN(JsonnetVm* vm_ptr, api->c_jsonnet_make());
  vm = std::make_unique<sapi::v::RemotePtr>(vm_ptr);

  if_jsonnet_vm_was_used = false;
  if_input_was_read = false;

  return;
}

// Clean up after a test.
void JsonnetTestHelper::TestTearDown() {
  if (if_jsonnet_vm_was_used) {
    SAPI_ASSERT_OK_AND_ASSIGN(char* result,
                              api->c_jsonnet_realloc(vm.get(), output.get(), 0));
  }
  ASSERT_THAT(api->c_jsonnet_destroy(vm.get()), sapi::IsOk());
  if (if_input_was_read) {
    ASSERT_THAT(api->c_free_input(input.get()), sapi::IsOk());
  }

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

  if_input_was_read = true;

  return;
}

// Evaluate jsonnet code.
void JsonnetTestHelper::Evaluate_jsonnet_code(char* filename, Evaluation type, bool expected_correct) {
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

  if (expected_correct) {
    ASSERT_THAT(error.GetValue(), testing::Eq(0));
  } else {
    ASSERT_THAT(error.GetValue(), testing::Eq(1));
  }

  output = std::make_unique<sapi::v::RemotePtr>(output_ptr);

  if_jsonnet_vm_was_used = true;

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
                                                   out_file_var.PtrBefore(), false));
      break;
    }

    case YAML_STREAM: {
      std::string out_file_in_sandboxee(std::string("/output/") +
                                        basename(&filename_or_directory[0]));
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

// Reading the output written to a file by library function / expected output
std::string JsonnetTestHelper::Read_output(char* filename) {
  std::ifstream input_stream(filename);
  std::string contents((std::istreambuf_iterator<char>(input_stream)), std::istreambuf_iterator<char>());
  return contents;
}
