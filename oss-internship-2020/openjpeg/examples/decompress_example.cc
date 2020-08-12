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

#include <iostream>
#include <cstdlib>
#include <cassert>

#include "convert_helper.h"

#include "openjp2_sapi.sapi.h"
#include "sandboxed_api/util/flag.h"

class Parameters : public sapi::v::Struct<opj_dparameters_t> {};
class Opj_image_t : public sapi::v::Struct<opj_image_t> {};

class Openjp2SapiSandbox : public Openjp2Sandbox {
 public:
  Openjp2SapiSandbox(const std::string& in_file)
      : in_file_(in_file) {}

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

	if (argc != 3) {
		std::cerr << "usage: " 
				  << basename(argv[0]) 
				  << " absolute/path/to/INPUT.jp2"
				  << " absolute/path/to/OUTPUT.pnm\n";
		return EXIT_FAILURE;
	}

	std::string in_file(argv[1]);

	Openjp2SapiSandbox sandbox(in_file);
	absl::Status status = sandbox.Init();
	assert(status.ok());

	Openjp2Api api(&sandbox);
	sapi::v::ConstCStr in_file_v(in_file.c_str());

	sapi::StatusOr<opj_stream_t*> stream_status 
		= api.opj_stream_create_default_file_stream(in_file_v.PtrBefore(), 1);
	assert(stream_status.ok());
	void* stream_status_value = stream_status.value();
	sapi::v::RemotePtr stream_pointer(stream_status_value);

	sapi::StatusOr<opj_codec_t*> codec_status 
		= api.opj_create_decompress(OPJ_CODEC_JP2);
	assert(codec_status.ok());
	void* codec_status_value = codec_status.value();
	sapi::v::RemotePtr codec_pointer(codec_status_value);

	Parameters parameters;
	status = api.opj_set_default_decoder_parameters(parameters.PtrBoth());
	assert(status.ok());

	sapi::StatusOr<OPJ_BOOL> bool_status 
		= api.opj_setup_decoder(&codec_pointer, parameters.PtrBefore());
	assert(bool_status.ok());
	assert(bool_status.value());

	sapi::v::GenericPtr image_pointer;
	bool_status = api.opj_read_header(&stream_pointer,
									  &codec_pointer,
									  image_pointer.PtrAfter());
	assert(bool_status.ok());
	assert(bool_status.value());

	Opj_image_t image;
	image.SetRemote((void*)image_pointer.GetValue());
	assert(sandbox.TransferFromSandboxee(&image).ok());

	bool_status = api.opj_decode(&codec_pointer,
								 &stream_pointer,
								 (sapi::v::Ptr*)&image_pointer);
	assert(bool_status.ok());
	assert(bool_status.value());

	bool_status = api.opj_end_decompress(&codec_pointer, &stream_pointer);
	assert(bool_status.ok());
	assert(bool_status.value());

	status = api.opj_stream_destroy(&stream_pointer);
	assert(status.ok());

	int components = image.data().numcomps;

	sapi::v::Array<opj_image_comp_t> image_components(components);
	image_components.SetRemote(image.data().comps);
	assert(sandbox.TransferFromSandboxee(&image_components).ok());

	image.mutable_data()->comps
		= (opj_image_comp_t*)image_components.GetLocal();

	int width = (int)image.data().comps[0].w;
	int height = (int)image.data().comps[0].h;

	int data[components][width * height];
	sapi::v::Array<int> image_components_data(width * height);

	for (int i = 0; i < components; i++) {
		image_components_data.SetRemote(image.data().comps[i].data);
		assert(sandbox.TransferFromSandboxee(&image_components_data).ok());
		for (int j = 0; j < width * height; j++) {
			data[i][j] = image_components_data[j];
		}
		image_components[i].data = data[i];
	}

	int error = imagetopnm((opj_image_t*)image.GetLocal(), argv[2], 0);
	assert(error == 0);

	status = api.opj_destroy_codec(&codec_pointer);
	assert(status.ok());

	sapi::v::RemotePtr remote_image_pointer(image.GetRemote());
	status = api.opj_image_destroy(&remote_image_pointer);
	assert(status.ok());

	return EXIT_SUCCESS;
}
