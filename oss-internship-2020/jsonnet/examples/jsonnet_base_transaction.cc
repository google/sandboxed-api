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

#include "jsonnet_base_transaction.h"

absl::Status JsonnetTransaction::Main() {

  JsonnetApi api(sandbox());

  SAPI_ASSIGN_OR_RETURN(JsonnetVm* jsonnet_vm, api.c_jsonnet_make());
  sapi::v::RemotePtr vm_pointer(jsonnet_vm);

  std::string in_file_in_sandboxee(std::string("/input/") +
                                   basename(&in_file_[0]));
  sapi::v::ConstCStr in_file_var(in_file_in_sandboxee.c_str());

  SAPI_ASSIGN_OR_RETURN(char* input, api.c_read_input(false, in_file_var.PtrBefore()));

  sapi::v::RemotePtr input_pointer(input);
  sapi::v::Int error;
  SAPI_ASSIGN_OR_RETURN(char* output, api.c_jsonnet_evaluate_snippet(&vm_pointer, in_file_var.PtrBefore(), &input_pointer, error.PtrAfter()));
  TRANSACTION_FAIL_IF_NOT(error.GetValue() != 0, "Jsonnet code evaluation failed.");

  std::string out_file_in_sandboxee(std::string("/output/") +
                                    basename(&out_file_[0]));
  sapi::v::ConstCStr out_file_var(out_file_in_sandboxee.c_str());
  sapi::v::RemotePtr output_pointer(output);

  SAPI_ASSIGN_OR_RETURN(bool success, api.c_write_output_file(&output_pointer, out_file_var.PtrBefore()));
  TRANSACTION_FAIL_IF_NOT(success, "Writing to output file failed.");

  SAPI_ASSIGN_OR_RETURN(char* result, api.c_jsonnet_realloc(&vm_pointer, &output_pointer, 0));

  SAPI_RETURN_IF_ERROR(api.c_jsonnet_destroy(&vm_pointer));
  SAPI_RETURN_IF_ERROR(api.c_free_input(&input_pointer));


  return absl::OkStatus();
}