#ifndef LIBPNG_WRAPPER_FUNC_H
#define LIBPNG_WRAPPER_FUNC_H

#include <cstdio>
#include "png.h"

extern "C" {
void* png_fdopen(int fd, const char *mode);
void png_rewind(void* f);
void png_fread(void* buffer, size_t size, size_t count, void* stream);
void png_fclose(void* f);
void png_setjmp(png_structrp ptr);
png_structp png_create_read_struct_wrapper(png_const_charp user_png_ver, png_voidp error_ptr);
png_structp png_create_write_struct_wrapper(png_const_charp user_png_ver, png_voidp error_ptr);
void png_init_io_wrapper(png_structrp png_ptr, void* f);
}
#endif