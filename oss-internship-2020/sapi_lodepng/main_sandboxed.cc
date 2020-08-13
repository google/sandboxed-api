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

#include <stdio.h>
#include <stdlib.h>

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "lodepng_sapi.sapi.h"
#include "sandbox.h"
#include "sandboxed_api/util/flag.h"

ABSL_DECLARE_FLAG(string, sandbox2_danger_danger_permit_all);
ABSL_DECLARE_FLAG(string, sandbox2_danger_danger_permit_all_and_log);
ABSL_FLAG(string, images_path, std::filesystem::current_path().string(),
          "path to the folder containing test images");

constexpr unsigned int img_len(unsigned int width, unsigned int height) {
  return width * height * 4;
}

void generate_one_step(SapiLodepngSandbox &sandbox, LodepngApi &api) {
  unsigned int width = 512, height = 512;
  std::vector<unsigned char> image(img_len(width, height));

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image[4 * width * y + 4 * x + 0] = 255 * !(x & y);
      image[4 * width * y + 4 * x + 1] = x ^ y;
      image[4 * width * y + 4 * x + 2] = x | y;
      image[4 * width * y + 4 * x + 3] = 255;
    }
  }

  // encode the image
  sapi::v::Array<unsigned char> sapi_image(image.data(), img_len(width, height));
  sapi::v::UInt sapi_width(width), sapi_height(height);
  std::string filename = "/output/out_generated1.png";
  sapi::v::ConstCStr sapi_filename(filename.c_str());

  sapi::StatusOr<unsigned int> result = api.lodepng_encode32_file(
      sapi_filename.PtrBefore(), sapi_image.PtrBefore(), sapi_width.GetValue(),
      sapi_height.GetValue());

  assert(result.ok());
  assert(!result.value());

  // after the image has been encoded, decode it to check that the
  // pixel values are the same

  sapi::v::UInt sapi_width2, sapi_height2;
  sapi::v::IntBase<unsigned char *> sapi_image_ptr(0);

  result = api.lodepng_decode32_file(
      sapi_image_ptr.PtrBoth(), sapi_width2.PtrBoth(), sapi_height2.PtrBoth(),
      sapi_filename.PtrBefore());
  assert(result.ok());
  assert(!result.value());

  assert(sapi_width2.GetValue() == width);
  assert(sapi_height2.GetValue() == height);

  // the pixels have been allocated inside the sandboxed process
  // memory, so we need to transfer them to this process.
  // Transferring the memory has the following steps:
  // 1) define a RemotePtr variable that holds the memory location from
  // the sandboxed process
  // 2) define an array with the required length
  // 3) set the remote pointer for the array to specify where the memory
  // that will be transferred is located
  // 4) transfer the memory to this process (this step is why we need
  // the pointer and the length)
  sapi::v::RemotePtr sapi_remote_out_ptr(sapi_image_ptr.GetValue());
  sapi::v::Array<unsigned char> sapi_pixels(
      img_len(sapi_width2.GetValue(), sapi_height2.GetValue()));
  sapi_pixels.SetRemote(sapi_remote_out_ptr.GetValue());

  assert(sandbox.TransferFromSandboxee(&sapi_pixels).ok());

  // after the memory has been transferred, we can access it
  // using the GetData function
  unsigned char *pixels_ptr = sapi_pixels.GetData();

  // now, we can compare the values
  for (size_t i = 0; i < img_len(width, height); ++i) {
    assert(pixels_ptr[i] == image[i]);
  }
}

void generate_two_steps(SapiLodepngSandbox &sandbox, LodepngApi &api) {
  // generate the values
  unsigned int width = 512, height = 512;
  std::vector<unsigned char> image(img_len(width, height));

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image[4 * width * y + 4 * x + 0] = 255 * !(x & y);
      image[4 * width * y + 4 * x + 1] = x ^ y;
      image[4 * width * y + 4 * x + 2] = x | y;
      image[4 * width * y + 4 * x + 3] = 255;
    }
  }

  // encode the image into memory first
  sapi::v::Array<unsigned char> sapi_image(image.data(), img_len(width, height));
  sapi::v::UInt sapi_width(width), sapi_height(height);
  std::string filename = "/output/out_generated2.png";
  sapi::v::ConstCStr sapi_filename(filename.c_str());

  sapi::v::ULLong sapi_pngsize;
  sapi::v::IntBase<unsigned char *> sapi_png_ptr(0);

  // encode it into memory
  sapi::StatusOr<unsigned int> result = api.lodepng_encode32(
      sapi_png_ptr.PtrBoth(), sapi_pngsize.PtrBoth(), sapi_image.PtrBefore(),
      sapi_width.GetValue(), sapi_height.GetValue());

  assert(result.ok());
  assert(!result.value());

  // the new array (pointed to by sapi_png_ptr) is allocated
  // inside the sandboxed process so we need to transfer it to this
  // process

  sapi::v::RemotePtr sapi_remote_out_ptr(sapi_png_ptr.GetValue());
  sapi::v::Array<unsigned char> sapi_png_array(sapi_pngsize.GetValue());

  sapi_png_array.SetRemote(sapi_remote_out_ptr.GetValue());

  assert(sandbox.TransferFromSandboxee(&sapi_png_array).ok());

  // write the image into the file (from memory)
  result =
      api.lodepng_save_file(sapi_png_array.PtrBefore(), sapi_pngsize.GetValue(),
                            sapi_filename.PtrBefore());

  assert(result.ok());
  assert(!result.value());

  // now, decode the image using the 2 steps in order to compare the values
  sapi::v::UInt sapi_width2, sapi_height2;
  sapi::v::IntBase<unsigned char *> sapi_png_ptr2(0);
  sapi::v::ULLong sapi_pngsize2;

  // load the file in memory
  result =
      api.lodepng_load_file(sapi_png_ptr2.PtrBoth(), sapi_pngsize2.PtrBoth(),
                            sapi_filename.PtrBefore());

  assert(result.ok());
  assert(!result.value());

  assert(sapi_pngsize.GetValue() == sapi_pngsize2.GetValue());

  // transfer the png array
  sapi::v::RemotePtr sapi_remote_out_ptr2(sapi_png_ptr2.GetValue());
  sapi::v::Array<unsigned char> sapi_png_array2(sapi_pngsize2.GetValue());

  sapi_png_array2.SetRemote(sapi_remote_out_ptr2.GetValue());

  assert(sandbox.TransferFromSandboxee(&sapi_png_array2).ok());

  // after the file is loaded, decode it so we have access to the values
  // directly
  sapi::v::IntBase<unsigned char *> sapi_png_ptr3(0);
  result = api.lodepng_decode32(
      sapi_png_ptr3.PtrBoth(), sapi_width2.PtrBoth(), sapi_height2.PtrBoth(),
      sapi_png_array2.PtrBefore(), sapi_pngsize2.GetValue());

  assert(result.ok());
  assert(!result.value());

  assert(sapi_width2.GetValue() == width);
  assert(sapi_height2.GetValue() == height);

  // transfer the pixels so they can be used
  sapi::v::RemotePtr sapi_remote_out_ptr3(sapi_png_ptr3.GetValue());
  sapi::v::Array<unsigned char> sapi_pixels(
      img_len(sapi_width2.GetValue(), sapi_height2.GetValue()));

  sapi_pixels.SetRemote(sapi_remote_out_ptr3.GetValue());

  assert(sandbox.TransferFromSandboxee(&sapi_pixels).ok());

  unsigned char *pixels_ptr = sapi_pixels.GetData();

  // compare values
  for (size_t i = 0; i < img_len(width, height); ++i) {
    assert(pixels_ptr[i] == image[i]);
  }
}

int main(int argc, char *argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  absl::Status ret;

  std::string images_path(absl::GetFlag(FLAGS_images_path));

  SapiLodepngSandbox sandbox(images_path);
  ret = sandbox.Init();
  if (!ret.ok()) {
    std::cerr << "error code: " << ret.code() << std::endl
              << "message: " << ret.message() << std::endl;
    exit(1);
  }

  LodepngApi api(&sandbox);

  generate_one_step(sandbox, api);
  generate_two_steps(sandbox, api);

  return EXIT_SUCCESS;
}