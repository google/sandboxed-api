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

#include <cstdint>
#include <iostream>
#include <vector>

#include "helpers.h"  // NOLINT(build/include)
#include "sandbox.h"  // NOLINT(build/include)
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/status/statusor.h"

void EncodeDecodeOneStep(SapiLodepngSandbox& sandbox, LodepngApi& api) {
  // Generate the values.
  std::vector<uint8_t> image = GenerateValues();

  // Encode the image.
  sapi::v::Array<uint8_t> sapi_image(kImgLen);
  CHECK(std::copy(image.begin(), image.end(), sapi_image.GetData()))
      << "Could not copy values";

  sapi::v::ConstCStr sapi_filename("/output/out_generated1.png");

  absl::StatusOr<unsigned int> result = api.lodepng_encode32_file(
      sapi_filename.PtrBefore(), sapi_image.PtrBefore(), kWidth, kHeight);

  CHECK(result.ok()) << "encode32_file call failed";
  CHECK(!result.value()) << "Unexpected result from encode32_file call";

  // After the image has been encoded, decode it to check that the
  // pixel values are the same.
  sapi::v::UInt sapi_width, sapi_height;
  sapi::v::IntBase<uint8_t*> sapi_image_ptr(0);

  result = api.lodepng_decode32_file(
      sapi_image_ptr.PtrBoth(), sapi_width.PtrBoth(), sapi_height.PtrBoth(),
      sapi_filename.PtrBefore());

  CHECK(result.ok()) << "decode32_file call failes";
  CHECK(!result.value()) << "Unexpected result from decode32_file call";

  CHECK(sapi_width.GetValue() == kWidth) << "Widths differ";
  CHECK(sapi_height.GetValue() == kHeight) << "Heights differ";

  // The pixels have been allocated inside the sandboxed process
  // memory, so we need to transfer them to this process.
  // Transferring the memory has the following steps:
  // 1) define an array with the required length.
  // 2) set the remote pointer for the array to specify where the memory
  // that will be transferred is located.
  // 3) transfer the memory to this process (this step is why we need
  // the pointer and the length).
  sapi::v::Array<uint8_t> sapi_pixels(kImgLen);
  sapi_pixels.SetRemote(sapi_image_ptr.GetValue());

  CHECK(sandbox.TransferFromSandboxee(&sapi_pixels).ok())
      << "Error during transfer from sandboxee";

  // Now, we can compare the values.
  CHECK(absl::equal(image.begin(), image.end(), sapi_pixels.GetData(),
                    sapi_pixels.GetData() + kImgLen))
      << "Values differ";

  // Free the memory allocated inside the sandbox.
  CHECK(sandbox.rpc_channel()->Free(sapi_image_ptr.GetValue()).ok())
      << "Could not free memory inside sandboxed process";
}

void EncodeDecodeTwoSteps(SapiLodepngSandbox& sandbox, LodepngApi& api) {
  // Generate the values.
  std::vector<uint8_t> image = GenerateValues();

  // Encode the image into memory first.
  sapi::v::Array<uint8_t> sapi_image(kImgLen);
  CHECK(std::copy(image.begin(), image.end(), sapi_image.GetData()))
      << "Could not copy values";

  sapi::v::ConstCStr sapi_filename("/output/out_generated2.png");

  sapi::v::ULLong sapi_pngsize;
  sapi::v::IntBase<uint8_t*> sapi_png_ptr(0);

  // Encode it into memory.
  absl::StatusOr<unsigned int> result =
      api.lodepng_encode32(sapi_png_ptr.PtrBoth(), sapi_pngsize.PtrBoth(),
                           sapi_image.PtrBefore(), kWidth, kHeight);

  CHECK(result.ok()) << "encode32 call failed";
  CHECK(!result.value()) << "Unexpected result from encode32 call";

  // The new array (pointed to by sapi_png_ptr) is allocated
  // inside the sandboxed process so we need to transfer it to this
  // process.
  sapi::v::Array<uint8_t> sapi_png_array(sapi_pngsize.GetValue());
  sapi_png_array.SetRemote(sapi_png_ptr.GetValue());

  CHECK(sandbox.TransferFromSandboxee(&sapi_png_array).ok())
      << "Error during transfer from sandboxee";

  // Write the image into the file (from memory).
  result =
      api.lodepng_save_file(sapi_png_array.PtrBefore(), sapi_pngsize.GetValue(),
                            sapi_filename.PtrBefore());

  CHECK(result.ok()) << "save_file call failed";
  CHECK(!result.value()) << "Unexpected result from save_file call";

  // Now, decode the image using the 2 steps in order to compare the values.
  sapi::v::UInt sapi_width, sapi_height;
  sapi::v::IntBase<uint8_t*> sapi_png_ptr2(0);
  sapi::v::ULLong sapi_pngsize2;

  // Load the file in memory.
  result =
      api.lodepng_load_file(sapi_png_ptr2.PtrBoth(), sapi_pngsize2.PtrBoth(),
                            sapi_filename.PtrBefore());

  CHECK(result.ok()) << "load_file call failed";
  CHECK(!result.value()) << "Unexpected result from load_file call";

  CHECK(sapi_pngsize.GetValue() == sapi_pngsize2.GetValue())
      << "Png sizes differ";

  // Transfer the png array.
  sapi::v::Array<uint8_t> sapi_png_array2(sapi_pngsize2.GetValue());
  sapi_png_array2.SetRemote(sapi_png_ptr2.GetValue());

  CHECK(sandbox.TransferFromSandboxee(&sapi_png_array2).ok())
      << "Error during transfer from sandboxee";

  // After the file is loaded, decode it so we have access to the values
  // directly.
  sapi::v::IntBase<uint8_t*> sapi_png_ptr3(0);
  result = api.lodepng_decode32(
      sapi_png_ptr3.PtrBoth(), sapi_width.PtrBoth(), sapi_height.PtrBoth(),
      sapi_png_array2.PtrBefore(), sapi_pngsize2.GetValue());

  CHECK(result.ok()) << "decode32 call failed";
  CHECK(!result.value()) << "Unexpected result from decode32 call";

  CHECK(sapi_width.GetValue() == kWidth) << "Widths differ";
  CHECK(sapi_height.GetValue() == kHeight) << "Heights differ";

  // Transfer the pixels so they can be used here.
  sapi::v::Array<uint8_t> sapi_pixels(kImgLen);
  sapi_pixels.SetRemote(sapi_png_ptr3.GetValue());

  CHECK(sandbox.TransferFromSandboxee(&sapi_pixels).ok())
      << "Error during transfer from sandboxee";

  // Compare the values.
  CHECK(absl::equal(image.begin(), image.end(), sapi_pixels.GetData(),
                    sapi_pixels.GetData() + kImgLen))
      << "Values differ";

  // Free the memory allocated inside the sandbox.
  CHECK(sandbox.rpc_channel()->Free(sapi_png_ptr.GetValue()).ok())
      << "Could not free memory inside sandboxed process";
  CHECK(sandbox.rpc_channel()->Free(sapi_png_ptr2.GetValue()).ok())
      << "Could not free memory inside sandboxed process";
  CHECK(sandbox.rpc_channel()->Free(sapi_png_ptr3.GetValue()).ok())
      << "Could not free memory inside sandboxed process";
}

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  const std::string images_path = CreateTempDirAtCWD();
  CHECK(sapi::file_util::fileops::Exists(images_path, false))
      << "Temporary directory does not exist";

  SapiLodepngSandbox sandbox(images_path);
  CHECK(sandbox.Init().ok()) << "Error during sandbox initialization";

  LodepngApi api(&sandbox);

  EncodeDecodeOneStep(sandbox, api);
  EncodeDecodeTwoSteps(sandbox, api);

  if (sapi::file_util::fileops::DeleteRecursively(images_path)) {
    LOG(WARNING) << "Temporary folder could not be deleted";
  }

  return EXIT_SUCCESS;
}
