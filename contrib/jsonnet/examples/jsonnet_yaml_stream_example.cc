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

#include <cstdlib>
#include <iostream>

#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "contrib/jsonnet/jsonnet_base_sandbox.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"

absl::Status JsonnetMain(std::string in_file, std::string out_file) {
  using sapi::file::JoinPath;
  using sapi::file_util::fileops::Basename;

  // Initialize sandbox.
  JsonnetBaseSandbox sandbox(in_file, out_file);
  SAPI_RETURN_IF_ERROR(sandbox.Init());

  JsonnetApi api(&sandbox);

  // Initialize library's main structure.
  SAPI_ASSIGN_OR_RETURN(JsonnetVm * jsonnet_vm, api.c_jsonnet_make());
  sapi::v::RemotePtr vm_pointer(jsonnet_vm);

  // Read input file.
  std::string in_file_in_sandboxee(JoinPath("/input", Basename(in_file)));
  sapi::v::ConstCStr in_file_var(in_file_in_sandboxee.c_str());
  SAPI_ASSIGN_OR_RETURN(char* input,
                        api.c_read_input(false, in_file_var.PtrBefore()));

  // Process jsonnet data.
  sapi::v::RemotePtr input_pointer(input);
  sapi::v::Int error;
  SAPI_ASSIGN_OR_RETURN(char* output, api.c_jsonnet_evaluate_snippet_stream(
                                          &vm_pointer, in_file_var.PtrBefore(),
                                          &input_pointer, error.PtrAfter()));
  CHECK(!error.GetValue())
      << "Jsonnet code evaluation failed: " << error.GetValue() << "\n"
      << "Make sure all files used by your jsonnet file are in the same "
         "directory as your file.";

  // Write data to file.
  std::string out_file_in_sandboxee(JoinPath("/output", Basename(out_file)));
  sapi::v::ConstCStr out_file_var(out_file_in_sandboxee.c_str());
  sapi::v::RemotePtr output_pointer(output);

  SAPI_ASSIGN_OR_RETURN(
      bool success,
      api.c_write_output_file(&output_pointer, out_file_var.PtrBefore()));
  CHECK(success) << "Writing to output file failed: " << success;

  // Clean up.
  SAPI_ASSIGN_OR_RETURN(char* result,
                        api.c_jsonnet_realloc(&vm_pointer, &output_pointer, 0));
  SAPI_RETURN_IF_ERROR(api.c_jsonnet_destroy(&vm_pointer));
  SAPI_RETURN_IF_ERROR(api.c_free_input(&input_pointer));

  return absl::OkStatus();
}

int main(int argc, char* argv[]) {
  using sapi::file_util::fileops::Basename;

  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  if (argc == 3) {
    std::cerr << "Usage:\n"
              << Basename(argv[0])
              << " /absolute/path/to/INPUT.jsonnet /absolute/path/to/OUTPUT\n";
    return EXIT_FAILURE;
  }

  std::string in_file(argv[1]);
  std::string out_file(argv[2]);

  absl::Status status = JsonnetMain(in_file, out_file);
  if (!status.ok()) {
    LOG(ERROR) << "Failed: " << status.ToString();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
