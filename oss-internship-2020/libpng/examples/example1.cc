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

#include <string>

#include "../sandboxed.h"     // NOLINT(build/include)
#include "../tests/libpng.h"  // NOLINT(build/include)
#include "sandboxed_api/vars.h"

absl::Status LibPNGMain(const std::string& infile, const std::string& outfile) {
  LibPNGSapiSandbox sandbox;
  sandbox.AddFile(infile);
  sandbox.AddFile(outfile);

  SAPI_RETURN_IF_ERROR(sandbox.Init());
  LibPNGApi api(&sandbox);

  sapi::v::Struct<png_image> image;
  sapi::v::ConstCStr infile_var(infile.c_str());
  sapi::v::ConstCStr outfile_var(outfile.c_str());

  image.mutable_data()->version = PNG_IMAGE_VERSION;

  SAPI_ASSIGN_OR_RETURN(
      int result, api.png_image_begin_read_from_file(image.PtrBoth(),
                                                     infile_var.PtrBefore()));
  if (!result) {
    return absl::InternalError(
        absl::StrCat("begin read error: ", image.mutable_data()->message));
  }

  image.mutable_data()->format = PNG_FORMAT_RGBA;

  sapi::v::Array<uint8_t> buffer(PNG_IMAGE_SIZE(*image.mutable_data()));

  sapi::v::NullPtr null = sapi::v::NullPtr();
  SAPI_ASSIGN_OR_RETURN(result,
                        api.png_image_finish_read(image.PtrBoth(), &null,
                                                  buffer.PtrBoth(), 0, &null));
  if (!result) {
    return absl::InternalError(
        absl::StrCat("finish read error: ", image.mutable_data()->message));
  }

  SAPI_ASSIGN_OR_RETURN(result, api.png_image_write_to_file(
                                    image.PtrBoth(), outfile_var.PtrBefore(), 0,
                                    buffer.PtrBoth(), 0, &null));
  if (!result) {
    return absl::InternalError(
        absl::StrCat("write error: ", image.mutable_data()->message));
  }

  return absl::OkStatus();
}

int main(int argc, char* argv[]) {
  if (argc != 3) {
    LOG(ERROR) << "usage: example input-file output-file";
    return EXIT_FAILURE;
  }

  auto status = LibPNGMain(argv[1], argv[2]);
  if (!status.ok()) {
    LOG(ERROR) << "LibPNGMain failed with error:\n"
               << status.ToString() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
