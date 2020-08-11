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

#include <glog/logging.h>
#include <stdlib.h>

#include <filesystem>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lodepng_sapi.sapi.h"
#include "sandbox.h"
#include "sandboxed_api/util/flag.h"

// #include <time.h>

// defining the flag does not work as intended (always has the default value)
// ignore for now
// ABSL_FLAG(string, images_path, std::filesystem::current_path().string(),
//           "path to the folder containing test images");

namespace {

// use the current path + test_images
std::string images_path =
    std::filesystem::current_path().string() + "/test_images";

TEST(initSandbox, basic) {
  SapiLodepngSandbox sandbox(images_path);
  ASSERT_TRUE(sandbox.Init().ok());
}

// generate an image, encode it, decode it and compare the pixels with the
// initial values
TEST(generate_image, encode_decode_compare_one_step) {
  SapiLodepngSandbox sandbox(images_path);
  ASSERT_TRUE(sandbox.Init().ok());
  LodepngApi api(&sandbox);
  // std::cout << "path = " << images_path << std::endl;
  unsigned int width = 512, height = 512;
  unsigned char *image = (unsigned char *)malloc(width * height * 4);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image[4 * width * y + 4 * x + 0] = 255 * !(x & y);
      image[4 * width * y + 4 * x + 1] = x ^ y;
      image[4 * width * y + 4 * x + 2] = x | y;
      image[4 * width * y + 4 * x + 3] = 255;
    }
  }

  sapi::v::Array<unsigned char> sapi_image(image, width * height * 4);
  sapi::v::UInt sapi_width(width), sapi_height(height);
  std::string filename = images_path + "/out_generated1.png";
  sapi::v::ConstCStr sapi_filename(filename.c_str());

  //   ASSERT_TRUE(sandbox.Allocate(&image_).ok());
  //   ASSERT_TRUE(sandbox.TransferToSandboxee(&image_).ok());

  sapi::StatusOr<unsigned int> result = api.lodepng_encode32_file(
      sapi_filename.PtrBefore(), sapi_image.PtrBefore(), sapi_width.GetValue(),
      sapi_height.GetValue());

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value(), 0);

  sapi::v::UInt sapi_width2, sapi_height2;
  sapi::v::IntBase<unsigned char *> sapi_image_ptr(0);

  result = api.lodepng_decode32_file(
      sapi_image_ptr.PtrBoth(), sapi_width2.PtrBoth(), sapi_height2.PtrBoth(),
      sapi_filename.PtrBefore());

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value(), 0);

  ASSERT_EQ(sapi_width2.GetValue(), width);
  ASSERT_EQ(sapi_height2.GetValue(), height);

  sapi::v::RemotePtr sapi_remote_out_ptr(
      reinterpret_cast<void *>(sapi_image_ptr.GetValue()));
  sapi::v::Array<unsigned char> sapi_pixels(sapi_width2.GetValue() *
                                            sapi_height2.GetValue() * 4);
  sapi_pixels.SetRemote(sapi_remote_out_ptr.GetValue());

  ASSERT_TRUE(sandbox.TransferFromSandboxee(&sapi_pixels).ok());

  unsigned char *pixels_ptr = sapi_pixels.GetData();

  for (size_t i = 0; i < width * height * 4; ++i) {
    ASSERT_EQ(pixels_ptr[i], image[i]);
  }

  free(image);
}

// similar to the previous test, only that we use encoding by saving the data in
// memory and then writing it to the file and decoding by first decoding in
// memory and then getting the pixels.
TEST(generate_image, encode_decode_compare_two_step) {
  SapiLodepngSandbox sandbox(images_path);
  ASSERT_TRUE(sandbox.Init().ok());
  LodepngApi api(&sandbox);

  // generate the image
  unsigned int width = 512, height = 512;
  unsigned char *image = (unsigned char *)malloc(width * height * 4);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image[4 * width * y + 4 * x + 0] = 255 * !(x & y);
      image[4 * width * y + 4 * x + 1] = x ^ y;
      image[4 * width * y + 4 * x + 2] = x | y;
      image[4 * width * y + 4 * x + 3] = 255;
    }
  }

  sapi::v::Array<unsigned char> sapi_image(image, width * height * 4);
  sapi::v::UInt sapi_width(width), sapi_height(height);
  std::string filename = images_path + "/out_generated2.png";
  sapi::v::ConstCStr sapi_filename(filename.c_str());

  sapi::v::ULLong sapi_pngsize;
  sapi::v::IntBase<unsigned char *> sapi_png_ptr(0);

  // encode it into memory

  sapi::StatusOr<unsigned int> result = api.lodepng_encode32(
      sapi_png_ptr.PtrBoth(), sapi_pngsize.PtrBoth(), sapi_image.PtrBefore(),
      sapi_width.GetValue(), sapi_height.GetValue());

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value(), 0);

  std::cout << "sapi_pngsize = " << sapi_pngsize.GetValue() << std::endl;

  // transfer the array from the sandboxed process

  sapi::v::RemotePtr sapi_remote_out_ptr(
      reinterpret_cast<void *>(sapi_png_ptr.GetValue()));
  sapi::v::Array<unsigned char> sapi_png_array(sapi_pngsize.GetValue());

  sapi_png_array.SetRemote(sapi_remote_out_ptr.GetValue());

  ASSERT_TRUE(sandbox.TransferFromSandboxee(&sapi_png_array).ok());

  // write the image into the file (from memory)
  result =
      api.lodepng_save_file(sapi_png_array.PtrBefore(), sapi_pngsize.GetValue(),
                            sapi_filename.PtrBefore());

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value(), 0);

  // now, decode the image using the 2 steps

  sapi::v::UInt sapi_width2, sapi_height2;
  sapi::v::IntBase<unsigned char *> sapi_png_ptr2(0);
  sapi::v::ULLong sapi_pngsize2;

  result =
      api.lodepng_load_file(sapi_png_ptr2.PtrBoth(), sapi_pngsize2.PtrBoth(),
                            sapi_filename.PtrBefore());

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value(), 0);

  ASSERT_EQ(sapi_pngsize.GetValue(), sapi_pngsize2.GetValue());

  sapi::v::RemotePtr sapi_remote_out_ptr2(
      reinterpret_cast<void *>(sapi_png_ptr2.GetValue()));
  sapi::v::Array<unsigned char> sapi_png_array2(sapi_pngsize2.GetValue());

  sapi_png_array2.SetRemote(sapi_remote_out_ptr2.GetValue());

  ASSERT_TRUE(sandbox.TransferFromSandboxee(&sapi_png_array2).ok());

  // after the file is loaded, decode it
  sapi::v::IntBase<unsigned char *> sapi_png_ptr3(0);
  //   sapi::v::UInt sapi_width2, sapi_height2;
  result = api.lodepng_decode32(
      sapi_png_ptr3.PtrBoth(), sapi_width2.PtrBoth(), sapi_height2.PtrBoth(),
      sapi_png_array2.PtrBefore(), sapi_pngsize2.GetValue());

  ASSERT_TRUE(result.ok());
  ASSERT_EQ(result.value(), 0);

  std::cout << "w2 = " << sapi_width2.GetValue()
            << " h2 = " << sapi_height2.GetValue() << std::endl;

  ASSERT_EQ(sapi_width2.GetValue(), width);
  ASSERT_EQ(sapi_height2.GetValue(), height);

  // transfer the pixels so they can be used
  sapi::v::RemotePtr sapi_remote_out_ptr3(
      reinterpret_cast<void *>(sapi_png_ptr3.GetValue()));
  sapi::v::Array<unsigned char> sapi_pixels(sapi_width2.GetValue() *
                                            sapi_height2.GetValue() * 4);

  sapi_pixels.SetRemote(sapi_remote_out_ptr3.GetValue());

  ASSERT_TRUE(sandbox.TransferFromSandboxee(&sapi_pixels).ok());

  unsigned char *pixels_ptr = sapi_pixels.GetData();

  // compare values
  for (size_t i = 0; i < width * height * 4; ++i) {
    ASSERT_EQ(pixels_ptr[i], image[i]);
  }

  free(image);
}

}  // namespace