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

// Perform decompression from *.jp2 to *.pnm format

#include <libgen.h>
#include <syscall.h>

#include <cstdlib>
#include <iostream>
#include <vector>

#include "gen_files/convert.h"  // NOLINT(build/include)
#include "openjp2_sapi.sapi.h"  // NOLINT(build/include)
#include "absl/status/statusor.h"

class Openjp2SapiSandbox : public Openjp2Sandbox {
 public:
  explicit Openjp2SapiSandbox(std::string in_file)
      : in_file_(std::move(in_file)) {}

  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
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
            __NR_lseek,
        })
        .AddFile(in_file_)
        .BuildOrDie();
  }

 private:
  std::string in_file_;
};

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  sapi::InitLogging(argv[0]);

  if (argc != 3) {
    std::cerr << "Usage: " << basename(argv[0]) << " absolute/path/to/INPUT.jp2"
              << " absolute/path/to/OUTPUT.pnm\n";
    return EXIT_FAILURE;
  }

  std::string in_file(argv[1]);

  // Initialize sandbox.
  Openjp2SapiSandbox sandbox(in_file);
  absl::Status status = sandbox.Init();
  CHECK(status.ok()) << "Sandbox initialization failed " << status;

  Openjp2Api api(&sandbox);
  sapi::v::ConstCStr in_file_v(in_file.c_str());

  // Initialize library's main data-holders.
  absl::StatusOr<opj_stream_t*> stream =
      api.opj_stream_create_default_file_stream(in_file_v.PtrBefore(), 1);
  CHECK(stream.ok()) << "Stream initialization failed: " << stream.status();
  sapi::v::RemotePtr stream_pointer(stream.value());

  absl::StatusOr<opj_codec_t*> codec = api.opj_create_decompress(OPJ_CODEC_JP2);
  CHECK(codec.ok()) << "Codec initialization failed: " << stream.status();
  sapi::v::RemotePtr codec_pointer(codec.value());

  sapi::v::Struct<opj_dparameters_t> parameters;
  status = api.opj_set_default_decoder_parameters(parameters.PtrBoth());
  CHECK(status.ok()) << "Parameters initialization failed " << status;

  absl::StatusOr<OPJ_BOOL> bool_status =
      api.opj_setup_decoder(&codec_pointer, parameters.PtrBefore());
  CHECK(bool_status.ok() && bool_status.value()) << "Decoder setup failed";

  // Start reading image from the input file.
  sapi::v::GenericPtr image_pointer;
  bool_status = api.opj_read_header(&stream_pointer, &codec_pointer,
                                    image_pointer.PtrAfter());
  CHECK(bool_status.ok() && bool_status.value())
      << "Reading image header failed";

  sapi::v::Struct<opj_image_t> image;
  image.SetRemote(reinterpret_cast<void*>(image_pointer.GetValue()));
  CHECK(sandbox.TransferFromSandboxee(&image).ok())
      << "Transfer from sandboxee failed";

  bool_status =
      api.opj_decode(&codec_pointer, &stream_pointer, image.PtrAfter());
  CHECK(bool_status.ok() && bool_status.value()) << "Decoding failed";

  bool_status = api.opj_end_decompress(&codec_pointer, &stream_pointer);
  CHECK(bool_status.ok() && bool_status.value()) << "Ending decompress failed";

  int components = image.data().numcomps;

  // Transfer the read data to the main process.
  sapi::v::Array<opj_image_comp_t> image_components(components);
  image_components.SetRemote(image.data().comps);
  CHECK(sandbox.TransferFromSandboxee(&image_components).ok())
      << "Transfer from sandboxee failed";

  image.mutable_data()->comps =
      static_cast<opj_image_comp_t*>(image_components.GetLocal());

  unsigned int width = static_cast<unsigned int>(image.data().comps[0].w);
  unsigned int height = static_cast<unsigned int>(image.data().comps[0].h);

  std::vector<std::vector<OPJ_INT32>> data(components);
  sapi::v::Array<OPJ_INT32> image_components_data(width * height);

  for (int i = 0; i < components; ++i) {
    image_components_data.SetRemote(image.data().comps[i].data);
    CHECK(sandbox.TransferFromSandboxee(&image_components_data).ok())
        << "Transfer from sandboxee failed";

    std::vector<OPJ_INT32> component_data(
        image_components_data.GetData(),
        image_components_data.GetData() + (width * height));
    data[i] = std::move(component_data);
    image_components[i].data = &data[i][0];
  }

  // Convert the image to the desired format and save it to the file.
  int error =
      imagetopnm(static_cast<opj_image_t*>(image.GetLocal()), argv[2], 0);
  CHECK(!error) << "Image convert failed";

  // Clean up.
  status = api.opj_image_destroy(image.PtrNone());
  CHECK(status.ok()) << "Image destroy failed " << status;

  status = api.opj_stream_destroy(&stream_pointer);
  CHECK(status.ok()) << "Stream destroy failed " << status;

  status = api.opj_destroy_codec(&codec_pointer);
  CHECK(status.ok()) << "Codec destroy failed " << status;

  return EXIT_SUCCESS;
}
