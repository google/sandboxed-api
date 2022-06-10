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

#include "helpers.h"  // NOLINT(build/include)
#include "sandbox.h"  // NOLINT(build/include)
#include "gtest/gtest.h"
#include "sandboxed_api/util/status_matchers.h"

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::NotNull;

namespace {

TEST(HelpersTest, CreateTempDirAtCWD) {
  const std::string images_path = CreateTempDirAtCWD();
  ASSERT_THAT(sapi::file_util::fileops::Exists(images_path, false), IsTrue())
      << "Temporary directory does not exist";

  EXPECT_THAT(sapi::file_util::fileops::DeleteRecursively(images_path),
              IsTrue())
      << "Temporary directory could not be deleted";
}

TEST(HelpersTest, GenerateValues) {
  EXPECT_THAT(GenerateValues().size(), Eq(kImgLen));
}

TEST(LodePngTest, Init) {
  const std::string images_path = CreateTempDirAtCWD();
  ASSERT_THAT(sapi::file_util::fileops::Exists(images_path, false), IsTrue())
      << "Temporary directory does not exist";

  SapiLodepngSandbox sandbox(images_path);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Error during sandbox init";

  EXPECT_THAT(sapi::file_util::fileops::DeleteRecursively(images_path),
              IsTrue())
      << "Temporary directory could not be deleted";
}

// Generate an image, encode it, decode it and compare the pixels with the
// initial values.
TEST(LodePngTest, EncodeDecodeOneStep) {
  const std::string images_path = CreateTempDirAtCWD();
  ASSERT_THAT(sapi::file_util::fileops::Exists(images_path, false), IsTrue())
      << "Temporary directory does not exist";

  SapiLodepngSandbox sandbox(images_path);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Error during sandbox initialization";
  LodepngApi api(&sandbox);

  std::vector<uint8_t> image = GenerateValues();

  sapi::v::Array<uint8_t> sapi_image(kImgLen);
  EXPECT_THAT(std::copy(image.begin(), image.end(), sapi_image.GetData()),
              IsTrue())
      << "Could not copy values";

  sapi::v::ConstCStr sapi_filename("/output/out_generated1.png");

  SAPI_ASSERT_OK_AND_ASSIGN(
      unsigned int result,
      api.lodepng_encode32_file(sapi_filename.PtrBefore(),
                                sapi_image.PtrBefore(), kWidth, kHeight));

  ASSERT_THAT(result, Eq(0)) << "Unexpected result from encode32_file call";

  sapi::v::UInt sapi_width, sapi_height;
  sapi::v::IntBase<uint8_t*> sapi_image_ptr(0);

  SAPI_ASSERT_OK_AND_ASSIGN(
      result, api.lodepng_decode32_file(
                  sapi_image_ptr.PtrBoth(), sapi_width.PtrBoth(),
                  sapi_height.PtrBoth(), sapi_filename.PtrBefore()));

  ASSERT_THAT(result, Eq(0)) << "Unexpected result from decode32_file call";

  EXPECT_THAT(sapi_width.GetValue(), Eq(kWidth)) << "Widths differ";
  EXPECT_THAT(sapi_height.GetValue(), Eq(kHeight)) << "Heights differ";

  sapi::v::Array<uint8_t> sapi_pixels(kImgLen);
  sapi_pixels.SetRemote(sapi_image_ptr.GetValue());

  ASSERT_THAT(sandbox.TransferFromSandboxee(&sapi_pixels), IsOk())
      << "Error during transfer from sandboxee";

  EXPECT_THAT(absl::equal(image.begin(), image.end(), sapi_pixels.GetData(),
                          sapi_pixels.GetData() + kImgLen),
              IsTrue())
      << "Values differ";

  EXPECT_THAT(sandbox.rpc_channel()->Free(sapi_image_ptr.GetValue()), IsOk())
      << "Could not free memory inside sandboxed process";

  EXPECT_THAT(sapi::file_util::fileops::DeleteRecursively(images_path),
              IsTrue())
      << "Temporary directory could not be deleted";
}

// Similar to the previous test, only that we use encoding by saving the data in
// memory and then writing it to the file and decoding by first decoding in
// memory and then getting the actual pixel values.
TEST(LodePngTest, EncodeDecodeTwoSteps) {
  const std::string images_path = CreateTempDirAtCWD();
  ASSERT_THAT(sapi::file_util::fileops::Exists(images_path, false), IsTrue())
      << "Temporary directory does not exist";

  SapiLodepngSandbox sandbox(images_path);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Error during sandbox init";
  LodepngApi api(&sandbox);

  std::vector<uint8_t> image = GenerateValues();

  sapi::v::Array<uint8_t> sapi_image(kImgLen);
  EXPECT_THAT(std::copy(image.begin(), image.end(), sapi_image.GetData()),
              IsTrue())
      << "Could not copy values";

  sapi::v::ConstCStr sapi_filename("/output/out_generated2.png");

  sapi::v::ULLong sapi_pngsize;
  sapi::v::IntBase<uint8_t*> sapi_png_ptr(0);

  SAPI_ASSERT_OK_AND_ASSIGN(
      unsigned int result,
      api.lodepng_encode32(sapi_png_ptr.PtrBoth(), sapi_pngsize.PtrBoth(),
                           sapi_image.PtrBefore(), kWidth, kHeight));

  ASSERT_THAT(result, Eq(0)) << "Unexpected result from encode32 call";

  sapi::v::Array<uint8_t> sapi_png_array(sapi_pngsize.GetValue());
  sapi_png_array.SetRemote(sapi_png_ptr.GetValue());

  ASSERT_THAT(sandbox.TransferFromSandboxee(&sapi_png_array), IsOk())
      << "Error during transfer from sandboxee";

  SAPI_ASSERT_OK_AND_ASSIGN(
      result,
      api.lodepng_save_file(sapi_png_array.PtrBefore(), sapi_pngsize.GetValue(),
                            sapi_filename.PtrBefore()));

  ASSERT_THAT(result, Eq(0)) << "Unexpected result from save_file call";

  sapi::v::UInt sapi_width, sapi_height;
  sapi::v::IntBase<uint8_t*> sapi_png_ptr2(0);
  sapi::v::ULLong sapi_pngsize2;

  SAPI_ASSERT_OK_AND_ASSIGN(
      result,
      api.lodepng_load_file(sapi_png_ptr2.PtrBoth(), sapi_pngsize2.PtrBoth(),
                            sapi_filename.PtrBefore()));

  ASSERT_THAT(result, Eq(0)) << "Unexpected result from load_file call";

  EXPECT_THAT(sapi_pngsize.GetValue(), Eq(sapi_pngsize2.GetValue()))
      << "Png sizes differ";

  sapi::v::Array<uint8_t> sapi_png_array2(sapi_pngsize2.GetValue());
  sapi_png_array2.SetRemote(sapi_png_ptr2.GetValue());

  ASSERT_THAT(sandbox.TransferFromSandboxee(&sapi_png_array2), IsOk())
      << "Error during transfer from sandboxee";

  sapi::v::IntBase<uint8_t*> sapi_png_ptr3(0);
  SAPI_ASSERT_OK_AND_ASSIGN(
      result,
      api.lodepng_decode32(sapi_png_ptr3.PtrBoth(), sapi_width.PtrBoth(),
                           sapi_height.PtrBoth(), sapi_png_array2.PtrBefore(),
                           sapi_pngsize2.GetValue()));

  ASSERT_THAT(result, Eq(0)) << "Unexpected result from decode32 call";

  EXPECT_THAT(sapi_width.GetValue(), Eq(kWidth)) << "Widths differ";
  EXPECT_THAT(sapi_height.GetValue(), Eq(kHeight)) << "Heights differ";

  sapi::v::Array<uint8_t> sapi_pixels(kImgLen);
  sapi_pixels.SetRemote(sapi_png_ptr3.GetValue());

  ASSERT_THAT(sandbox.TransferFromSandboxee(&sapi_pixels), IsOk())
      << "Error during transfer from sandboxee";

  EXPECT_THAT(absl::equal(image.begin(), image.end(), sapi_pixels.GetData(),
                          sapi_pixels.GetData() + kImgLen),
              IsTrue())
      << "Values differ";

  EXPECT_THAT(sandbox.rpc_channel()->Free(sapi_png_ptr.GetValue()), IsOk());
  EXPECT_THAT(sandbox.rpc_channel()->Free(sapi_png_ptr2.GetValue()), IsOk());
  EXPECT_THAT(sandbox.rpc_channel()->Free(sapi_png_ptr3.GetValue()), IsOk());

  EXPECT_THAT(sapi::file_util::fileops::DeleteRecursively(images_path),
              IsTrue())
      << "Temporary directory could not be deleted";
}

}  // namespace
