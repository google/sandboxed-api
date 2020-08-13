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
#include <filesystem>
#include <iostream>

#include "lodepng/lodepng.h"

constexpr unsigned int img_len(unsigned int width, unsigned int height) {
  return width * height * 4;
}

void generate_one_step(const std::string &images_path) {
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
  std::string filename = images_path + "/out_generated1.png";
  unsigned int result =
      lodepng_encode32_file(filename.c_str(), image.data(), width, height);

  assert(!result);

  // after the image has been encoded, decode it to check that the
  // pixel values are the same

  unsigned int width2, height2;
  unsigned char *image2 = 0;

  result = lodepng_decode32_file(&image2, &width2, &height2, filename.c_str());

  assert(!result);

  assert(width2 == width);
  assert(height2 == height);

  // now, we can compare the values
  for (size_t i = 0; i < img_len(width, height); ++i) {
    assert(image2[i] == image[i]);
  }
}

void generate_two_steps(const std::string &images_path) {
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
  std::string filename = images_path + "/out_generated2.png";
  unsigned char *png;
  size_t pngsize;

  unsigned int result =
      lodepng_encode32(&png, &pngsize, image.data(), width, height);

  assert(!result);

  // write the image into the file (from memory)
  result = lodepng_save_file(png, pngsize, filename.c_str());

  assert(!result);

  // now, decode the image using the 2 steps in order to compare the values
  unsigned int width2, height2;
  unsigned char *png2;
  size_t pngsize2;

  // load the file in memory
  result = lodepng_load_file(&png2, &pngsize2, filename.c_str());

  assert(!result);
  assert(pngsize == pngsize2);

  unsigned char *image2;
  result = lodepng_decode32(&image2, &width2, &height2, png2, pngsize2);

  assert(!result);
  assert(width2 == width);
  assert(height2 == height);

  // compare values
  for (size_t i = 0; i < img_len(width, height); ++i) {
    assert(image2[i] == image[i]);
  }
}

int main(int argc, char *argv[]) {
  std::string images_path = std::filesystem::current_path().string();

  generate_one_step(images_path);
  generate_two_steps(images_path);

  return 0;
}