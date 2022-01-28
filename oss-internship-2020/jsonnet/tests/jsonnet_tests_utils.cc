// Copyright 2020 Google LLC
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

#include "jsonnet_tests.h"  // NOLINT(build/include)

// Prepares what is needed to perform a test.
void JsonnetTestHelper::TestSetUp() {
  // Get paths to where input and output is stored.
  char buffer[256];
  int error = readlink("/proc/self/exe", buffer, 256);
  ASSERT_GE(error, 0);

  std::pair<absl::string_view, absl::string_view> parts_of_path =
      sapi::file::SplitPath(buffer);
  absl::string_view binary_path = parts_of_path.first;

  std::string input_path =
      sapi::file::JoinPath(binary_path, "tests_input", "dummy_input");
  std::string output_path =
      sapi::file::JoinPath(binary_path, "tests_output", "dummy_input");

  // Set up sandbox and api.
  sandbox_ = absl::make_unique<JsonnetBaseSandbox>(input_path, output_path);
  ASSERT_THAT(sandbox_->Init(), sapi::IsOk());
  api_ = absl::make_unique<JsonnetApi>(sandbox_.get());

  // Initialize library's main structure.
  SAPI_ASSERT_OK_AND_ASSIGN(JsonnetVm * vm_ptr, api_->c_jsonnet_make());
  vm_ = absl::make_unique<sapi::v::RemotePtr>(vm_ptr);

  jsonnet_vm_was_used_ = false;
  input_was_read_ = false;
}

// Cleans up after a test.
void JsonnetTestHelper::TestTearDown() {
  if (jsonnet_vm_was_used_) {
    SAPI_ASSERT_OK_AND_ASSIGN(
        char* result, api_->c_jsonnet_realloc(vm_.get(), output_.get(), 0));
  }
  ASSERT_THAT(api_->c_jsonnet_destroy(vm_.get()), sapi::IsOk());
  if (input_was_read_) {
    ASSERT_THAT(api_->c_free_input(input_.get()), sapi::IsOk());
  }
}

// Reads input from file.
void JsonnetTestHelper::ReadInput(const char* filename) {
  std::string in_file_in_sandboxee(std::string("/input/") +
                                   basename(const_cast<char*>(&filename[0])));
  input_filename_in_sandboxee_ = std::move(in_file_in_sandboxee);
  sapi::v::ConstCStr in_file_var(input_filename_in_sandboxee_.c_str());

  SAPI_ASSERT_OK_AND_ASSIGN(char* input_ptr,
                            api_->c_read_input(0, in_file_var.PtrBefore()));
  input_ = absl::make_unique<sapi::v::RemotePtr>(input_ptr);

  input_was_read_ = true;
}

// Evaluates jsonnet code.
void JsonnetTestHelper::EvaluateJsonnetCode(Evaluation type,
                                            bool expected_correct) {
  sapi::v::ConstCStr in_file_var(input_filename_in_sandboxee_.c_str());
  sapi::v::Int error;
  char* output_ptr;

  switch (type) {
    case kBase: {
      SAPI_ASSERT_OK_AND_ASSIGN(
          output_ptr,
          api_->c_jsonnet_evaluate_snippet(vm_.get(), in_file_var.PtrBefore(),
                                           input_.get(), error.PtrAfter()));
      break;
    }

    case kMultipleFiles: {
      SAPI_ASSERT_OK_AND_ASSIGN(
          output_ptr, api_->c_jsonnet_evaluate_snippet_multi(
                          vm_.get(), in_file_var.PtrBefore(), input_.get(),
                          error.PtrAfter()));
      break;
    }

    case kYamlStream: {
      SAPI_ASSERT_OK_AND_ASSIGN(
          output_ptr, api_->c_jsonnet_evaluate_snippet_stream(
                          vm_.get(), in_file_var.PtrBefore(), input_.get(),
                          error.PtrAfter()));
      break;
    }
  }

  if (expected_correct) {
    ASSERT_THAT(error.GetValue(), testing::Eq(0));
  } else {
    ASSERT_THAT(error.GetValue(), testing::Eq(1));
  }

  output_ = absl::make_unique<sapi::v::RemotePtr>(output_ptr);

  jsonnet_vm_was_used_ = true;
}

// Writes output to file.
void JsonnetTestHelper::WriteOutput(const char* filename_or_directory,
                                    Evaluation type) {
  bool success;

  switch (type) {
    case kBase: {
      std::string out_file_in_sandboxee(
          std::string("/output/") +
          basename(const_cast<char*>(&filename_or_directory[0])));
      sapi::v::ConstCStr out_file_var(out_file_in_sandboxee.c_str());

      SAPI_ASSERT_OK_AND_ASSIGN(
          success,
          api_->c_write_output_file(output_.get(), out_file_var.PtrBefore()));
      break;
    }
    case kMultipleFiles: {
      std::string out_file_in_sandboxee(std::string("/output/"));
      sapi::v::ConstCStr out_file_var(out_file_in_sandboxee.c_str());
      SAPI_ASSERT_OK_AND_ASSIGN(
          success, api_->c_write_multi_output_files(
                       output_.get(), out_file_var.PtrBefore(), false));
      break;
    }

    case kYamlStream: {
      std::string out_file_in_sandboxee(
          std::string("/output/") +
          basename(const_cast<char*>(&filename_or_directory[0])));
      sapi::v::ConstCStr out_file_var(out_file_in_sandboxee.c_str());
      SAPI_ASSERT_OK_AND_ASSIGN(
          success,
          api_->c_write_output_stream(output_.get(), out_file_var.PtrBefore()));
      break;
    }
  }

  ASSERT_THAT(success, testing::Eq(true));
}

// Reads the output written to a file by library function / expected output
std::string JsonnetTestHelper::ReadOutput(const char* filename) {
  std::ifstream input_stream(filename);
  std::string contents((std::istreambuf_iterator<char>(input_stream)),
                       std::istreambuf_iterator<char>());
  return contents;
}
