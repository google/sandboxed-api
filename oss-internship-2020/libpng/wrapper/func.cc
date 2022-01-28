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

#include <cstdlib>

void png_setjmp(png_structrp ptr) { setjmp(png_jmpbuf(ptr)); }

void* png_fdopen(int fd, const char* mode) {
  FILE* f = fdopen(fd, mode);
  return static_cast<void*>(f);
}

void png_rewind(void* f) { rewind(static_cast<FILE*>(f)); }

void png_fread(void* buffer, size_t size, size_t count, void* stream) {
  fread(buffer, size, count, static_cast<FILE*>(stream));
}

void png_fclose(void* f) { fclose(static_cast<FILE*>(f)); }

void png_init_io_wrapper(png_structrp png_ptr, void* f) {
  png_init_io(png_ptr, static_cast<FILE*>(f));
}

png_structp png_create_read_struct_wrapper(png_const_charp user_png_ver,
                                           png_voidp error_ptr) {
  return png_create_read_struct(user_png_ver, error_ptr, NULL, NULL);
}

png_structp png_create_write_struct_wrapper(png_const_charp user_png_ver,
                                            png_voidp error_ptr) {
  return png_create_write_struct(user_png_ver, error_ptr, NULL, NULL);
}

void png_read_image_wrapper(png_structrp png_ptr, png_bytep image,
                            size_t height, size_t rowbytes) {
  png_bytep* ptrs = (png_bytep*)malloc(height * sizeof(png_bytep));
  for (size_t i = 0; i != height; ++i) {
    ptrs[i] = image + (i * rowbytes);
  }
  png_read_image(png_ptr, ptrs);
  free(ptrs);
}

void png_write_image_wrapper(png_structrp png_ptr, png_bytep image,
                             size_t height, size_t rowbytes) {
  png_bytep* ptrs = (png_bytep*)malloc(height * sizeof(png_bytep));
  for (size_t i = 0; i != height; ++i) {
    ptrs[i] = image + (i * rowbytes);
  }
  png_write_image(png_ptr, ptrs);
  free(ptrs);
}
