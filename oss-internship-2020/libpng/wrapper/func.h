#ifndef LIBPNG_WRAPPER_FUNC_H
#define LIBPNG_WRAPPER_FUNC_H

#include <cstdio>

extern "C" {
FILE* png_fopen(const char* filename, const char* mode);
}
#endif