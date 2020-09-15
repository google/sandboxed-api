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

#include <libgen.h>
#include <syscall.h>

#include <cstdlib>
#include <iostream>

#include "jsonnet_sapi.sapi.h"
#include "sandboxed_api/util/flag.h"

class JsonnetSapiSandbox : public JsonnetSandbox {
 public:
  explicit JsonnetSapiSandbox(std::string in_file, std::string out_file)
      : in_file_(std::move(in_file)), out_file_(std::move(out_file)) {}

  // We need only the input file here, not the whole input directory
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder *) override {
    return sandbox2::PolicyBuilder()
        .AllowStaticStartup()
        .AllowOpen()
        .AllowRead()
        .AllowWrite()
        .AllowStat()
        .AllowSystemMalloc()
        .AllowExit()
        .AllowSyscalls({
            __NR_futex,
            __NR_close,
        })
        .AddDirectoryAt(dirname(&out_file_[0]), "/output", /*is_ro=*/false)
        .AddFile(in_file_, true)
        .BuildOrDie();
  }

 private:
  std::string in_file_;
  std::string out_file_;
};

int main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (!(argc == 3)) {
    std::cerr << "Usage:\n"
              << basename(argv[0]) << " absolute/path/to/INPUT.jsonnet"
              << " absolute/path/to/OUTPUT.jsonnet\n";
    return EXIT_FAILURE;
  }

  std::string in_file(argv[1]);
  std::string out_file(argv[2]);

  // Initialize sandbox.
  JsonnetSapiSandbox sandbox(in_file, out_file);
  absl::Status status = sandbox.Init();
  CHECK(status.ok()) << "Sandbox initialization failed " << status;

  JsonnetApi api(&sandbox);

  // Initialize library's main structure.
  sapi::StatusOr<JsonnetVm *> jsonnet_vm = api.c_jsonnet_make();
  sapi::v::RemotePtr vm_pointer(jsonnet_vm.value());
  CHECK(jsonnet_vm.ok()) << "JsonnetVm initialization failed: "
                         << jsonnet_vm.status();

  // Read input file.
  sapi::v::ConstCStr in_file_var(in_file.c_str());
  sapi::StatusOr<char *> input =
      api.c_read_input(false, in_file_var.PtrBefore());
  CHECK(input.ok()) << "Reading input file failed " << input.status();

  // Process jsonnet data.
  sapi::v::RemotePtr input_pointer(input.value());
  sapi::v::Int error;
  sapi::StatusOr<char *> output = api.c_jsonnet_fmt_snippet(
      &vm_pointer, in_file_var.PtrBefore(), &input_pointer, error.PtrAfter());
  CHECK(output.ok() && !error.GetValue())
      << "Jsonnet code evaluation failed: " << output.status() << " "
      << error.GetValue();

  // Write data to file.
  std::string out_file_in_sandboxee(std::string("/output/") +
                                    basename(&out_file[0]));
  sapi::v::ConstCStr out_file_var(out_file_in_sandboxee.c_str());
  sapi::v::RemotePtr output_pointer(output.value());
  sapi::StatusOr<bool> success;

  success = api.c_write_output_file(&output_pointer, out_file_var.PtrBefore());
  CHECK(success.ok() && success.value())
      << "Writing to output file failed " << success.status() << " "
      << success.value();

  // Clean up.
  sapi::StatusOr<char *> result =
      api.c_jsonnet_realloc(&vm_pointer, &output_pointer, 0);
  CHECK(result.ok()) << "JsonnetVm realloc failed: " << result.status();

  status = api.c_jsonnet_destroy(&vm_pointer);
  CHECK(status.ok()) << "JsonnetVm destroy failed: " << status;

  status = api.c_free_input(&input_pointer);
  CHECK(status.ok()) << "Input freeing failed: " << status;

  return EXIT_SUCCESS;
}
