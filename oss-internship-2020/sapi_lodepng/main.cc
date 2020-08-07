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

#include <iostream>

#include "lodepng-master/lodepng.h"

bool cmp_images32(const std::string &f1, const std::string &f2) {
  std::cout << "COMPARING IMAGES " << basename(f1.c_str()) << " -> "
            << basename(f2.c_str()) << std::endl;

  unsigned int error, width1, height1;
  unsigned char *image1 = 0;

  unsigned int width2, height2;
  unsigned char *image2 = 0;

  error = lodepng_decode32_file(&image1, &width1, &height1, f1.c_str());

  if (error) {
    std::cerr << "error " << error << ": " << lodepng_error_text(error)
              << std::endl;
    return false;
  }

  error = lodepng_decode32_file(&image2, &width2, &height2, f2.c_str());

  if (error) {
    std::cerr << "error " << error << ": " << lodepng_error_text(error)
              << std::endl;
    return false;
  }

  if (width1 != width2 || height1 != height2) {
    std::cerr << "DIMENSIONS DIFFER\n";
    return false;
  }

  for (int i = 0; i < width1 * height1; ++i) {
    if (image1[i] != image2[i]) {
      std::cerr << "PIXELS DIFFER AT i = " << i << std::endl;
      return false;
    }
  }
  return true;
}

// copies an image into another and compares them
void decode_and_encode32(const std::string &filename1,
                         const std::string &filename2) {
  unsigned int error, width, height;
  unsigned char *image = 0;

  error = lodepng_decode32_file(&image, &width, &height, filename1.c_str());

  if (error) {
    std::cerr << "error " << error << ": " << lodepng_error_text(error)
              << std::endl;
    return;
  }

  error = lodepng_encode32_file(filename2.c_str(), image, width, height);

  if (error) {
    std::cerr << "error " << error << ": " << lodepng_error_text(error)
              << std::endl;
    return;
  }

  free(image);
}

void test1(const std::string &images_path) {
  std::cout << "test1" << std::endl;

  std::string filename1 = images_path + "/test1.png";
  std::string filename2 = images_path + "/out/test1_1out.png";
  std::string filename3 = images_path + "/out/test1_2out.png";

  decode_and_encode32(filename1, filename2);
  decode_and_encode32(filename1, filename3);

  if (!cmp_images32(filename1, filename2)) {
    std::cout << "files are different" << std::endl;
  } else {
    std::cout << "files are not different" << std::endl;
  }

  if (!cmp_images32(filename3, filename2)) {
    std::cout << "files are different" << std::endl;
  } else {
    std::cout << "files are not different" << std::endl;
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cout << "usage: " << basename(argv[0]) << " images_folder_path"
              << std::endl;
    return 1;
  }

  std::string images_path(argv[1]);

  test1(images_path);
  return 0;
}