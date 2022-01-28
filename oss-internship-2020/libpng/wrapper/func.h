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

#ifndef LIBPNG_WRAPPER_FUNC_H_
#define LIBPNG_WRAPPER_FUNC_H_

#include "png.h"  // NOLINT(build/include)

extern "C" {

void* png_fdopen(int fd, const char* mode);
void png_rewind(void* f);
void png_fread(void* buffer, size_t size, size_t count, void* stream);
void png_fclose(void* f);
void png_setjmp(png_structrp ptr);
png_structp png_create_read_struct_wrapper(png_const_charp user_png_ver,
                                           png_voidp error_ptr);
png_structp png_create_write_struct_wrapper(png_const_charp user_png_ver,
                                            png_voidp error_ptr);
void png_init_io_wrapper(png_structrp png_ptr, void* f);
void png_read_image_wrapper(png_structrp png_ptr, png_bytep image,
                            size_t height, size_t rowbytes);
void png_write_image_wrapper(png_structrp png_ptr, png_bytep image,
                             size_t height, size_t rowbytes);

}  // extern "C"

#endif  // LIBPNG_WRAPPER_FUNC_H_
