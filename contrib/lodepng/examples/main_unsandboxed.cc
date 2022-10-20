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

#include "helpers.h"      // NOLINT(build/include)
#include "lodepng.gen.h"  // NOLINT(build/include)
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"

void EncodeDecodeOneStep(const std::string& images_path) {
  // Generate the values.
  std::vector<uint8_t> image = GenerateValues();

  // Encode the image.
  const std::string filename =
      sapi::file::JoinPath(images_path, "/out_generated1.png");
  unsigned int result =
      lodepng_encode32_file(filename.c_str(), image.data(), kWidth, kHeight);

  CHECK(!result) << "Unexpected result from encode32_file call";

  // After the image has been encoded, decode it to check that the
  // pixel values are the same.
  unsigned int width, height;
  uint8_t* image2 = 0;

  result = lodepng_decode32_file(&image2, &width, &height, filename.c_str());

  CHECK(!result) << "Unexpected result from decode32_file call";

  CHECK(width == kWidth) << "Widths differ";
  CHECK(height == kHeight) << "Heights differ";

  // Now, we can compare the values.
  CHECK(absl::equal(image.begin(), image.end(), image2, image2 + kImgLen))
      << "Values differ";

  free(image2);
}

void EncodeDecodeTwoSteps(const std::string& images_path) {
  // Generate the values.
  std::vector<uint8_t> image = GenerateValues();

  // Encode the image into memory first.
  const std::string filename =
      sapi::file::JoinPath(images_path, "/out_generated2.png");
  uint8_t* png;
  size_t pngsize;

  unsigned int result =
      lodepng_encode32(&png, &pngsize, image.data(), kWidth, kHeight);

  CHECK(!result) << "Unexpected result from encode32 call";

  // Write the image into the file (from memory).
  result = lodepng_save_file(png, pngsize, filename.c_str());

  CHECK(!result) << "Unexpected result from save_file call";

  // Now, decode the image using the 2 steps in order to compare the values.
  unsigned int width, height;
  uint8_t* png2;
  size_t pngsize2;

  // Load the file in memory.
  result = lodepng_load_file(&png2, &pngsize2, filename.c_str());

  CHECK(!result) << "Unexpected result from load_file call";
  CHECK(pngsize == pngsize2) << "Png sizes differ";

  uint8_t* image2;
  result = lodepng_decode32(&image2, &width, &height, png2, pngsize2);

  CHECK(!result) << "Unexpected result from decode32 call";
  CHECK(width == kWidth) << "Widths differ";
  CHECK(height == kHeight) << "Heights differ";

  // Compare the values.
  CHECK(absl::equal(image.begin(), image.end(), image2, image2 + kImgLen))
      << "Values differ";

  free(png);
  free(png2);
  free(image2);
}

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  const std::string images_path = CreateTempDirAtCWD();
  CHECK(sapi::file_util::fileops::Exists(images_path, false))
      << "Temporary directory does not exist";

  EncodeDecodeOneStep(images_path);
  EncodeDecodeTwoSteps(images_path);

  if (sapi::file_util::fileops::DeleteRecursively(images_path)) {
    LOG(WARNING) << "Temporary folder could not be deleted";
  }

  return EXIT_SUCCESS;
}
