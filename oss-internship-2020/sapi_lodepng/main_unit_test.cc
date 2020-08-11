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

#include <filesystem>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lodepng_sapi.sapi.h"
#include "sandbox.h"
#include "sandboxed_api/util/flag.h"
#include <stdlib.h>
#include <time.h>

// defining the flag does not work as intended (always has the default value)
// ignore for now
// ABSL_FLAG(string, images_path, std::filesystem::current_path().string(),
//           "path to the folder containing test images");

namespace {

// TODO change this into pwd/something else
std::string images_path = "/usr/local/google/home/amedar/internship/sandboxed-api/oss-internship-2020/sapi_lodepng/test_images";

TEST(initSandbox, basic) {
    SapiLodepngSandbox sandbox(images_path);
    ASSERT_TRUE(sandbox.Init().ok());
}

TEST(encode32, generate_and_encode_one_step) {
    // randomly generate pixels of an image and encode it into a file
    SapiLodepngSandbox sandbox(images_path);
    ASSERT_TRUE(sandbox.Init().ok());
    LodepngApi api(&sandbox);


    srand(time(NULL)); // maybe use something else
    unsigned int width = 512, height = 512;
    unsigned char *image = (unsigned char*)malloc(width * height * 4);

    for(int y = 0; y < height; ++y) {
    for(int x = 0; x < width; ++x) {
    image[4 * width * y + 4 * x + 0] = 255 * !(x & y);
    image[4 * width * y + 4 * x + 1] = x ^ y;
    image[4 * width * y + 4 * x + 2] = x | y;
    image[4 * width * y + 4 * x + 3] = 255;
  }
    }

    sapi::v::Array<unsigned char> image_(image, width * height);
    sapi::v::UInt width_(width), height_(height);
    std::string filename = images_path + "/out/generate_and_encode_one_step1.png";
    sapi::v::ConstCStr filename_(filename.c_str());

    ASSERT_TRUE(sandbox.Allocate(&image_).ok());
    ASSERT_TRUE(sandbox.TransferToSandboxee(&image_).ok());

    auto res = api.lodepng_encode32_file(filename_.PtrBefore(), image_.PtrBefore(), width_.GetValue(), height_.GetValue()).value();
    free(image);
}

}  // namespace