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

#include "gtest/gtest.h"
#include "helpers.h"
#include "sandbox.h"
#include "sandboxed_api/util/status_matchers.h"

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::NotNull;
using ::testing::IsTrue;

namespace {

TEST(HelpersTest, CreateTempDirAtCWD) {
  const std::string images_path = CreateTempDirAtCWD();
    EXPECT_THAT(sandbox2::file_util::fileops::Exists(images_path, false), IsTrue());

  ASSERT_THAT(sandbox2::file_util::fileops::DeleteRecursively(images_path),
              IsTrue());
}

TEST(HelpersTest, GenerateValues) {
    EXPECT_THAT(GenerateValues().size(), Eq(kImgLen));
}

TEST(LodePngTest, Init) {
  const std::string images_path = CreateTempDirAtCWD();
  SapiLodepngSandbox sandbox(images_path);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Error during sandbox init";

  ASSERT_THAT(sandbox2::file_util::fileops::DeleteRecursively(images_path),
              IsTrue());
}

// generate an image, encode it, decode it and compare the pixels with the
// initial values
TEST(LodePngTest, EncodeDecodeOneStep) {
  const std::string images_path = CreateTempDirAtCWD();

  SapiLodepngSandbox sandbox(images_path);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Error during sandbox init";
  LodepngApi api(&sandbox);

  // generate the values
  std::vector<uint8_t> image(GenerateValues());

  // encode the image
  sapi::v::Array<uint8_t> sapi_image(image.data(), kImgLen);
  sapi::v::ConstCStr sapi_filename("/output/out_generated1.png");

  SAPI_ASSERT_OK_AND_ASSIGN(
      unsigned int result,
      api.lodepng_encode32_file(sapi_filename.PtrBefore(),
                                sapi_image.PtrBefore(), kWidth, kHeight));

  ASSERT_THAT(result, Eq(0)) << "Result from encode32_file not 0";

  // after the image has been encoded, decode it to check that the
  // pixel values are the same
  sapi::v::UInt sapi_width2, sapi_height2;
  sapi::v::IntBase<uint8_t *> sapi_image_ptr(0);

  SAPI_ASSERT_OK_AND_ASSIGN(
      result, api.lodepng_decode32_file(
                  sapi_image_ptr.PtrBoth(), sapi_width2.PtrBoth(),
                  sapi_height2.PtrBoth(), sapi_filename.PtrBefore()));

  ASSERT_THAT(result, Eq(0)) << "Result from decode32_file not 0";

  EXPECT_THAT(sapi_width2.GetValue(), Eq(kWidth)) << "Widths differ";
  EXPECT_THAT(sapi_height2.GetValue(), Eq(kHeight)) << "Heights differ";

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
  sapi::v::Array<uint8_t> sapi_pixels(kImgLen);
  sapi_pixels.SetRemote(sapi_image_ptr.GetValue());

  ASSERT_THAT(sandbox.TransferFromSandboxee(&sapi_pixels), IsOk())
      << "Error during transfer from sandboxee";

  // now, we can compare the values
  EXPECT_THAT(std::equal(image.begin(), image.end(), sapi_pixels.GetData()),
              IsTrue())
      << "values differ";

  ASSERT_THAT(sandbox2::file_util::fileops::DeleteRecursively(images_path),
              IsTrue());
}

// similar to the previous test, only that we use encoding by saving the data in
// memory and then writing it to the file and decoding by first decoding in
// memory and then getting the actual pixel values.
TEST(LodePngTest, EncodeDecodeTwoSteps) {
  const std::string images_path = CreateTempDirAtCWD();

  SapiLodepngSandbox sandbox(images_path);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Error during sandbox init";
  LodepngApi api(&sandbox);

  // generate the values
  std::vector<uint8_t> image(GenerateValues());

  // encode the image into memory first
  sapi::v::Array<uint8_t> sapi_image(image.data(), kImgLen);
  sapi::v::ConstCStr sapi_filename("/output/out_generated2.png");

  sapi::v::ULLong sapi_pngsize;
  sapi::v::IntBase<uint8_t *> sapi_png_ptr(0);

  // encode it into memory
  SAPI_ASSERT_OK_AND_ASSIGN(
      unsigned int result,
      api.lodepng_encode32(sapi_png_ptr.PtrBoth(), sapi_pngsize.PtrBoth(),
                           sapi_image.PtrBefore(), kWidth, kHeight));

  ASSERT_THAT(result, Eq(0)) << "Result from encode32 call not 0";

  // the new array (pointed to by sapi_png_ptr) is allocated
  // inside the sandboxed process so we need to transfer it to this
  // process
  sapi::v::Array<uint8_t> sapi_png_array(sapi_pngsize.GetValue());
  sapi_png_array.SetRemote(sapi_png_ptr.GetValue());

  ASSERT_THAT(sandbox.TransferFromSandboxee(&sapi_png_array), IsOk())
      << "Error during transfer from sandboxee";

  // write the image into the file (from memory)
  SAPI_ASSERT_OK_AND_ASSIGN(
      result,
      api.lodepng_save_file(sapi_png_array.PtrBefore(), sapi_pngsize.GetValue(),
                            sapi_filename.PtrBefore()));

  ASSERT_THAT(result, Eq(0)) << "Result from save_file call not 0";

  // now, decode the image using the 2 steps in order to compare the values
  sapi::v::UInt sapi_width2, sapi_height2;
  sapi::v::IntBase<uint8_t *> sapi_png_ptr2(0);
  sapi::v::ULLong sapi_pngsize2;

  // load the file in memory
  SAPI_ASSERT_OK_AND_ASSIGN(
      result,
      api.lodepng_load_file(sapi_png_ptr2.PtrBoth(), sapi_pngsize2.PtrBoth(),
                            sapi_filename.PtrBefore()));

  ASSERT_THAT(result, Eq(0)) << "Result from load_file call not 0";

  EXPECT_THAT(sapi_pngsize.GetValue(), Eq(sapi_pngsize2.GetValue()))
      << "Png sizes differ";

  // transfer the png array
  sapi::v::Array<uint8_t> sapi_png_array2(sapi_pngsize2.GetValue());
  sapi_png_array2.SetRemote(sapi_png_ptr2.GetValue());

  ASSERT_THAT(sandbox.TransferFromSandboxee(&sapi_png_array2), IsOk())
      << "Error during transfer from sandboxee";

  // after the file is loaded, decode it so we have access to the values
  // directly
  sapi::v::IntBase<uint8_t *> sapi_png_ptr3(0);
  SAPI_ASSERT_OK_AND_ASSIGN(
      result,
      api.lodepng_decode32(sapi_png_ptr3.PtrBoth(), sapi_width2.PtrBoth(),
                           sapi_height2.PtrBoth(), sapi_png_array2.PtrBefore(),
                           sapi_pngsize2.GetValue()));

  ASSERT_THAT(result, Eq(0)) << "Result from decode32 call not 0";

  EXPECT_THAT(sapi_width2.GetValue(), Eq(kWidth)) << "Widths differ";
  EXPECT_THAT(sapi_height2.GetValue(), Eq(kHeight)) << "Heights differ";

  // transfer the pixels so they can be used
  sapi::v::Array<uint8_t> sapi_pixels(kImgLen);
  sapi_pixels.SetRemote(sapi_png_ptr3.GetValue());

  ASSERT_THAT(sandbox.TransferFromSandboxee(&sapi_pixels), IsOk())
      << "Error during transfer from sandboxee";

  // now we can compare values
  EXPECT_THAT(std::equal(image.begin(), image.end(), sapi_pixels.GetData()),
              IsTrue())
      << "values differ";

  ASSERT_THAT(sandbox2::file_util::fileops::DeleteRecursively(images_path),
              IsTrue());
}

}  // namespace