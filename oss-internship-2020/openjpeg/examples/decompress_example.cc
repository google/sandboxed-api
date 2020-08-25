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

// Perform decompression from *.jp2 to *.pnm format

#include <libgen.h>
#include <syscall.h>

#include <cstdlib>
#include <iostream>
#include <vector>

#include "absl/algorithm/container.h"
#include "convert_helper.h"
#include "openjp2_sapi.sapi.h"

class Openjp2SapiSandbox : public Openjp2Sandbox {
 public:
  Openjp2SapiSandbox(const std::string in_file)
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
  google::InitGoogleLogging(argv[0]);

  if (argc != 3) {
    std::cerr << "usage: " << basename(argv[0]) << " absolute/path/to/INPUT.jp2"
              << " absolute/path/to/OUTPUT.pnm\n";
    return EXIT_FAILURE;
  }

  std::string in_file(argv[1]);

  // initialize sandbox
  Openjp2SapiSandbox sandbox(in_file);
  absl::Status status = sandbox.Init();
  if (!status.ok()) {
    LOG(FATAL) << "sandbox initialization status: " << status;
    return EXIT_FAILURE;
  }

  Openjp2Api api(&sandbox);
  sapi::v::ConstCStr in_file_v(in_file.c_str());

  // initialize library's main data-holders
  sapi::StatusOr<opj_stream_t*> stream =
      api.opj_stream_create_default_file_stream(in_file_v.PtrBefore(), 1);
  if (!stream.ok()) {
    LOG(FATAL) << "opj_stream initialization failed: " << stream.status();
    return EXIT_FAILURE;
  }
  sapi::v::RemotePtr stream_pointer(stream.value());

  sapi::StatusOr<opj_codec_t*> codec = api.opj_create_decompress(OPJ_CODEC_JP2);
  if (!codec.ok()) {
    LOG(FATAL) << "opj_codec initialization failed: " << codec.status();
    return EXIT_FAILURE;
  }
  sapi::v::RemotePtr codec_pointer(codec.value());

  sapi::v::Struct<opj_dparameters_t> parameters;
  status = api.opj_set_default_decoder_parameters(parameters.PtrBoth());
  if (!status.ok()) {
    LOG(FATAL) << "parameters initialization failed: " << status;
    return EXIT_FAILURE;
  }

  sapi::StatusOr<OPJ_BOOL> bool_status =
      api.opj_setup_decoder(&codec_pointer, parameters.PtrBefore());
  if (!bool_status.ok() || !bool_status.value()) {
    LOG(FATAL) << "decoder setup failed";
    return EXIT_FAILURE;
  }

  // start reading image from the input file
  sapi::v::GenericPtr image_pointer;
  bool_status = api.opj_read_header(&stream_pointer, &codec_pointer,
                                    image_pointer.PtrAfter());
  if (!bool_status.ok() || !bool_status.value()) {
    LOG(FATAL) << "reading image header failed";
    return EXIT_FAILURE;
  }

  sapi::v::Struct<opj_image_t> image;
  image.SetRemote((void*)image_pointer.GetValue());
  if (!sandbox.TransferFromSandboxee(&image).ok()) {
    LOG(FATAL) << "transfer from sandboxee failed";
    return EXIT_FAILURE;
  }

  sapi::v::RemotePtr image_remote_pointer(image.GetRemote());
  bool_status =
      api.opj_decode(&codec_pointer, &stream_pointer, &image_remote_pointer);
  if (!bool_status.ok() || !bool_status.value()) {
    LOG(FATAL) << "decoding failed";
    return EXIT_FAILURE;
  }

  bool_status = api.opj_end_decompress(&codec_pointer, &stream_pointer);
  if (!bool_status.ok() || !bool_status.value()) {
    LOG(FATAL) << "ending decompress failed";
    return EXIT_FAILURE;
  }

  int components = image.data().numcomps;

  // transfer the read data to the main process
  sapi::v::Array<opj_image_comp_t> image_components(components);
  image_components.SetRemote(image.data().comps);
  if (!sandbox.TransferFromSandboxee(&image_components).ok()) {
    LOG(FATAL) << "transfer from sandboxee failed";
    return EXIT_FAILURE;
  }

  image.mutable_data()->comps = (opj_image_comp_t*)image_components.GetLocal();

  int width = (int)image.data().comps[0].w;
  int height = (int)image.data().comps[0].h;

  std::vector<std::vector<OPJ_INT32>> data;
  sapi::v::Array<OPJ_INT32> image_components_data(width * height);

  for (int i = 0; i < components; ++i) {
    image_components_data.SetRemote(image.data().comps[i].data);
    if (!sandbox.TransferFromSandboxee(&image_components_data).ok()) {
      LOG(FATAL) << "transfer from sandboxee failed";
      return EXIT_FAILURE;
    }
    std::vector<OPJ_INT32> component_data(
        image_components_data.GetData(),
        image_components_data.GetData() + (width * height));
    data.push_back(component_data);
  }

  for (int i = 0; i < components; ++i) {
    image_components[i].data = &data[i][0];
  }

  // convert the image to the desired format and save it to the file
  int error = imagetopnm((opj_image_t*)image.GetLocal(), argv[2], 0);
  if (error) LOG(FATAL) << "image convert failed";

  // cleanup
  status = api.opj_image_destroy(image.PtrNone());
  if (!status.ok()) LOG(FATAL) << "image destroy failed: " << status;

  status = api.opj_stream_destroy(&stream_pointer);
  if (!status.ok()) LOG(FATAL) << "stream destroy failed: " << status;

  status = api.opj_destroy_codec(&codec_pointer);
  if (!status.ok()) LOG(FATAL) << "codec destroy failed: " << status;

  return EXIT_SUCCESS;
}
