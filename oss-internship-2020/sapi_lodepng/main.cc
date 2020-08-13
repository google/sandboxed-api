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
// #include <libgen.h>

#include "lodepng/lodepng.h"

// bool cmp_images32(const std::string &f1, const std::string &f2) {
//   std::cout << "COMPARING IMAGES " << basename(f1.c_str()) << " -> "
//             << basename(f2.c_str()) << std::endl;

//   unsigned int error, width1, height1;
//   unsigned char *image1 = 0;

//   unsigned int width2, height2;
//   unsigned char *image2 = 0;

//   error = lodepng_decode32_file(&image1, &width1, &height1, f1.c_str());

//   if (error) {
//     std::cerr << "error " << error << ": " << lodepng_error_text(error)
//               << std::endl;
//     return false;
//   }

//   error = lodepng_decode32_file(&image2, &width2, &height2, f2.c_str());

//   if (error) {
//     std::cerr << "error " << error << ": " << lodepng_error_text(error)
//               << std::endl;
//     return false;
//   }

//   if (width1 != width2 || height1 != height2) {
//     std::cerr << "DIMENSIONS DIFFER\n";
//     return false;
//   }

//   std::cout << "width height = " << width1 << " " << height1 << std::endl;

//   for (int i = 0; i < width1 * height1; ++i) {
//     if (image1[i] != image2[i]) {
//       std::cerr << "PIXELS DIFFER AT i = " << i << std::endl;
//       return false;
//     }
//   }
//   return true;
// }

// // copies an image into another and compares them
// void decode_and_encode32(const std::string &filename1,
//                          const std::string &filename2) {
//   unsigned int error, width, height;
//   unsigned char *image = 0;

//   error = lodepng_decode32_file(&image, &width, &height, filename1.c_str());

//   if (error) {
//     std::cerr << "error " << error << ": " << lodepng_error_text(error)
//               << std::endl;
//     return;
//   }

//   error = lodepng_encode32_file(filename2.c_str(), image, width, height);

//   if (error) {
//     std::cerr << "error " << error << ": " << lodepng_error_text(error)
//               << std::endl;
//     return;
//   }

//   free(image);
// }

// void test1(const std::string &images_path) {
//   std::cout << "test1" << std::endl;

//   std::string filename1 = images_path + "/test1.png";
//   std::string filename2 = images_path + "/out/test1_1out.png";
//   std::string filename3 = images_path + "/out/test1_2out.png";

//   decode_and_encode32(filename1, filename2);
//   decode_and_encode32(filename1, filename3);

//   if (!cmp_images32(filename1, filename2)) {
//     std::cout << "files are different" << std::endl;
//   } else {
//     std::cout << "files are not different" << std::endl;
//   }

//   if (!cmp_images32(filename3, filename2)) {
//     std::cout << "files are different" << std::endl;
//   } else {
//     std::cout << "files are not different" << std::endl;
//   }
// }

// void encodeOneStep(const char *filename, const unsigned char *image,
//                    unsigned width, unsigned height) {
//   /*Encode the image*/
//   unsigned error = lodepng_encode32_file(filename, image, width, height);

//   /*if there's an error, display it*/
//   if (error) printf("error %u: %s\n", error, lodepng_error_text(error));
// }

// void test2() {
//   const char *filename = "test_images/out/ok.png";
//   unsigned width = 512, height = 512;
//   unsigned char *image = (unsigned char *)malloc(width * height * 4);
//   unsigned x, y;
//   for (y = 0; y < height; y++) {
//     for (x = 0; x < width; x++) {
//       image[4 * width * y + 4 * x + 0] = 255 * !(x & y);
//       image[4 * width * y + 4 * x + 1] = x ^ y;
//       image[4 * width * y + 4 * x + 2] = x | y;
//       image[4 * width * y + 4 * x + 3] = 255;
//     }
//   }

//   /*run an example*/
//   // encodeOneStep(filename, image, width, height);
//   lodepng_encode32_file(filename, image, width, height);
// }

void generate_one_step(const std::string &images_path) {
  unsigned int width = 512, height = 512;
  unsigned char *image = (unsigned char *)malloc(width * height * 4);
  assert(image);

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
      lodepng_encode32_file(filename.c_str(), image, width, height);

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
  for (size_t i = 0; i < width * height * 4; ++i) {
    assert(image2[i] == image[i]);
  }

  free(image);
}

void generate_two_steps(const std::string &images_path) {
  // generate the values
  unsigned int width = 512, height = 512;
  unsigned char *image = (unsigned char *)malloc(width * height * 4);

  assert(image);

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

  unsigned int result = lodepng_encode32(&png, &pngsize, image, width, height);

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

  // after the file is loaded, decode it so we have access to the values
  // directly
  //   sapi::v::IntBase<unsigned char *> sapi_png_ptr3(0);
  //   result = api.lodepng_decode32(
  //       sapi_png_ptr3.PtrBoth(), sapi_width2.PtrBoth(),
  //       sapi_height2.PtrBoth(), sapi_png_array2.PtrBefore(),
  //       sapi_pngsize2.GetValue());

  unsigned char *image2;
  result = lodepng_decode32(&image2, &width2, &height2, png2, pngsize2);

  assert(!result);

  assert(width2 == width);
  assert(height2 == height);

  // compare values
  for (size_t i = 0; i < width * height * 4; ++i) {
    assert(image2[i] == image[i]);
  }

  free(image);
}

int main(int argc, char *argv[]) {
  std::string images_path = std::filesystem::current_path().string();

  generate_one_step(images_path);
  generate_two_steps(images_path);

  return 0;
}