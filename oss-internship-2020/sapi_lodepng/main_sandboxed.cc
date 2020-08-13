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

// // takes a png image (f1), decodes it and ecodes it into f2.
// // can be viewed as copying f1 into f2. This function has a basic usage
// // of the decode and encode functions.
// void decode_and_encode32(SapiLodepngSandbox &sandbox, LodepngApi &api,
//                          const std::string &f1, const std::string &f2) {
//   sapi::v::UInt width(0), height(0);
//   sapi::v::ConstCStr filename1(f1.c_str()), filename2(f2.c_str());
//   absl::Status ret;

//   // in order to pass unsigned char ** to the function, we pass a variable
//   that
//   // contains the pointer.
//   sapi::v::IntBase<unsigned char *> image(0);

//   if (!api.lodepng_decode32_file(image.PtrBoth(), width.PtrBoth(),
//                                  height.PtrBoth(), filename1.PtrBefore())
//            .ok()) {
//     std::cerr << "decode failed" << std::endl;
//     exit(1);
//   }

//   // after the function is called, we need to access the data stored at the
//   // address to which the previous variable points. To do that, we have to
//   // transfer the data from the sandbox memory to this process's memory.
//   sapi::v::RemotePtr remote_out_ptr(reinterpret_cast<void
//   *>(image.GetValue())); sapi::v::Array<unsigned char>
//   out_img(width.GetValue() * height.GetValue());
//   out_img.SetRemote(remote_out_ptr.GetValue());

//   if (!sandbox.TransferFromSandboxee(&out_img).ok()) {
//     std::cerr << "Transfer From Sandboxee failed" << std::endl;
//     exit(1);
//   }

//   // now the values are available at out_img.GetData()

//   // when calling the encoding function, we need only an unsigned char *
//   // (instead of **) so we can simply use the sapi::v::Array defined before
//   // (PtrBefore will give us a pointer).
//   if (!api.lodepng_encode32_file(filename2.PtrBefore(), out_img.PtrBefore(),
//                                  width.GetValue(), height.GetValue())
//            .ok()) {
//     std::cerr << "encode failed" << std::endl;
//     exit(1);
//   }

//   // Since in this function we do not actually used the pixels, we could
//   simply
//   // call the encode function with the pointer from before (the remote
//   pointer,
//   // since the memory has already been allocated on the sandboxed process).
//   // However, most of the use cases of this library require accessing the
//   pixels
//   // which is why the solution in which data is transferred around is used.
// }

// // this seems to not work as intended at the moment
// void test2(SapiLodepngSandbox &sandbox, LodepngApi &api,
//            const std::string &images_path) {
//   // srand(time(NULL)); // maybe use something else
//   // int width = 1024, height = 1024;
//   unsigned int width = 512, height = 512;
//   unsigned char *image = (unsigned char *)malloc(width * height * 4);
//   // for (int i = 0; i < width * height; ++i) {
//   //     image[i] = rand() % 256;
//   // }

//   for (int y = 0; y < height; y++) {
//     for (int x = 0; x < width; x++) {
//       image[4 * width * y + 4 * x + 0] = 255 * !(x & y);
//       image[4 * width * y + 4 * x + 1] = x ^ y;
//       image[4 * width * y + 4 * x + 2] = x | y;
//       image[4 * width * y + 4 * x + 3] = 255;
//     }
//   }

//   sapi::v::Array<unsigned char> image_(image, width * height * 4);
//   sapi::v::UInt width_(width), height_(height);
//   std::string filename = images_path + "/out/ok2.png";
//   sapi::v::ConstCStr filename_(filename.c_str());

//   // sandbox.Allocate(&image_).IgnoreError();
//   // sandbox.TransferToSandboxee(&image_).IgnoreError();

//   api.lodepng_encode32_file(filename_.PtrBefore(), image_.PtrBefore(),
//                             width_.GetValue(), height_.GetValue())
//       .IgnoreError();
//   free(image);
// }

// // compares the pixels of the f1 and f2 png files.
// bool cmp_images32(SapiLodepngSandbox &sandbox, LodepngApi &api,
//                   const std::string &f1, const std::string &f2) {
//   std::cout << "COMPARING IMAGES " << basename(f1.c_str()) << " --- "
//             << basename(f2.c_str()) << std::endl;

//   sapi::v::UInt width1, height1, width2, height2;
//   sapi::v::ConstCStr filename1(f1.c_str()), filename2(f2.c_str());
//   sapi::v::IntBase<unsigned char *> image1_ptr(0), image2_ptr(0);
//   //   absl::Status ret;

//   if (!api.lodepng_decode32_file(image1_ptr.PtrBoth(), width1.PtrBoth(),
//                                  height1.PtrBoth(), filename1.PtrBefore())
//            .ok()) {
//     std::cerr << "decode failed" << std::endl;
//     exit(1);
//   }

//   if (!api.lodepng_decode32_file(image2_ptr.PtrBoth(), width2.PtrBoth(),
//                                  height2.PtrBoth(), filename2.PtrBefore())
//            .ok()) {
//     std::cerr << "decode failed" << std::endl;
//     exit(1);
//   }

//   sapi::v::RemotePtr remote_out_ptr1(
//       reinterpret_cast<void *>(image1_ptr.GetValue()));
//   sapi::v::Array<char> pixels1(width1.GetValue() * height1.GetValue());
//   pixels1.SetRemote(remote_out_ptr1.GetValue());

//   if (!sandbox.TransferFromSandboxee(&pixels1).ok()) {
//     std::cerr << "Transfer From Sandboxee failed" << std::endl;
//     exit(1);
//   }

//   sapi::v::RemotePtr remote_out_ptr2(
//       reinterpret_cast<void *>(image2_ptr.GetValue()));
//   sapi::v::Array<char> pixels2(width2.GetValue() * height2.GetValue());
//   pixels2.SetRemote(remote_out_ptr2.GetValue());

//   if (!sandbox.TransferFromSandboxee(&pixels2).ok()) {
//     std::cerr << "Transfer From Sandboxee failed" << std::endl;
//     exit(1);
//   }

//   if (width1.GetValue() != width2.GetValue() ||
//       height1.GetValue() != height2.GetValue()) {
//     std::cerr << "DIMENSIONS DIFFER\n";
//     return false;
//   }

//   for (size_t i = 0; i < width1.GetValue() * height1.GetValue(); ++i) {
//     if (pixels1.GetData()[i] != pixels2.GetData()[i]) {
//       std::cerr << "PIXELS DIFFER AT i = " << i << std::endl;
//       return false;
//     }
//   }

//   return true;
// }

// // this test simply copies the png from filename1 to filename2 and filename3
// // and then decodes those 2 files and compares the pixels. If those pixels
// are
// // equal, then encoding and decoding worked.
// void test1(SapiLodepngSandbox &sandbox, LodepngApi &api,
//            const std::string &images_path) {
//   std::string filename1 = images_path + "/test1.png";
//   std::string filename2 = images_path + "/out/test1_1out.png";
//   std::string filename3 = images_path + "/out/test1_2out.png";

//   decode_and_encode32(sandbox, api, filename1, filename2);
//   decode_and_encode32(sandbox, api, filename1, filename3);

//   if (!cmp_images32(sandbox, api, filename1, filename2)) {
//     std::cout << "files are different" << std::endl;
//   } else {
//     std::cout << "files are not different" << std::endl;
//   }

//   if (!cmp_images32(sandbox, api, filename3, filename2)) {
//     std::cout << "files are different" << std::endl;
//   } else {
//     std::cout << "files are not different" << std::endl;
//   }
// }

void generate_one_step(SapiLodepngSandbox &sandbox, LodepngApi &api,
                       const std::string &images_path) {
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
  sapi::v::Array<unsigned char> sapi_image(image, width * height * 4);
  sapi::v::UInt sapi_width(width), sapi_height(height);
  std::string filename = images_path + "/out_generated1.png";
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
  // 1) define a RemotePtr variable
  sapi::v::RemotePtr sapi_remote_out_ptr(
      reinterpret_cast<void *>(sapi_image_ptr.GetValue()));
  sapi::v::Array<unsigned char> sapi_pixels(sapi_width2.GetValue() *
                                            sapi_height2.GetValue() * 4);
  sapi_pixels.SetRemote(sapi_remote_out_ptr.GetValue());

  assert(sandbox.TransferFromSandboxee(&sapi_pixels).ok());

  // after the memory has been transferred, we can access it
  // using the GetData function
  unsigned char *pixels_ptr = sapi_pixels.GetData();

  // now, we can compare the values
  for (size_t i = 0; i < width * height * 4; ++i) {
    assert(pixels_ptr[i] == image[i]);
  }

  free(image);
}

void generate_two_steps(SapiLodepngSandbox &sandbox, LodepngApi &api,
                        const std::string &images_path) {
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

  assert(result.ok());
  assert(!result.value());

  // the new array (pointed to by sapi_png_ptr) is allocated
  // inside the sandboxed process so we need to transfer it to this
  // process

  sapi::v::RemotePtr sapi_remote_out_ptr(
      reinterpret_cast<void *>(sapi_png_ptr.GetValue()));
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
  sapi::v::RemotePtr sapi_remote_out_ptr2(
      reinterpret_cast<void *>(sapi_png_ptr2.GetValue()));
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
  sapi::v::RemotePtr sapi_remote_out_ptr3(
      reinterpret_cast<void *>(sapi_png_ptr3.GetValue()));
  sapi::v::Array<unsigned char> sapi_pixels(sapi_width2.GetValue() *
                                            sapi_height2.GetValue() * 4);

  sapi_pixels.SetRemote(sapi_remote_out_ptr3.GetValue());

  assert(sandbox.TransferFromSandboxee(&sapi_pixels).ok());

  unsigned char *pixels_ptr = sapi_pixels.GetData();

  // compare values
  for (size_t i = 0; i < width * height * 4; ++i) {
    assert(pixels_ptr[i] == image[i]);
  }

  free(image);
}

int main(int argc, char *argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  absl::Status ret;

  std::string images_path(absl::GetFlag(FLAGS_images_path));

  std::cout << "flag = " << images_path << std::endl;

  SapiLodepngSandbox sandbox(images_path);
  ret = sandbox.Init();
  if (!ret.ok()) {
    std::cerr << "error code: " << ret.code() << std::endl
              << "message: " << ret.message() << std::endl;
    exit(1);
  }

  LodepngApi api(&sandbox);

  generate_one_step(sandbox, api, images_path);
  generate_two_steps(sandbox, api, images_path);

  return EXIT_SUCCESS;
}