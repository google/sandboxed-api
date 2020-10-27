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

#ifndef LIBTIFF_WRAPPER_FUNC_H
#define LIBTIFF_WRAPPER_FUNC_H

#include "tiffio.h"  // NOLINT(build/include)

// wrapper for variadic functions TIFFGetField and TIFFSetField
// s - signed
// u - unsigned

extern "C" {
int TIFFGetField1(TIFF* tif, unsigned tag, void* param);
int TIFFGetField2(TIFF* tif, unsigned tag, void* param1, void* param2);
int TIFFGetField3(TIFF* tif, unsigned tag, void* param1, void* param2,
                  void* param3);

int TIFFSetFieldUChar1(TIFF* tif, unsigned tag, unsigned char param);
int TIFFSetFieldUChar2(TIFF* tif, unsigned tag, unsigned char param1,
                       unsigned char param2);
int TIFFSetFieldUChar3(TIFF* tif, unsigned tag, unsigned char param1,
                       unsigned char param2, unsigned char param3);

int TIFFSetFieldSChar1(TIFF* tif, unsigned tag, signed char param);
int TIFFSetFieldSChar2(TIFF* tif, unsigned tag, signed char param1,
                       signed char param2);
int TIFFSetFieldSChar3(TIFF* tif, unsigned tag, signed char param1,
                       signed char param2, signed char param3);

int TIFFSetFieldU1(TIFF* tif, unsigned tag, unsigned param);
int TIFFSetFieldU2(TIFF* tif, unsigned tag, unsigned param1, unsigned param2);
int TIFFSetFieldU3(TIFF* tif, unsigned tag, unsigned param1, unsigned param2,
                   unsigned param3);

int TIFFSetFieldS1(TIFF* tif, unsigned tag, int param);
int TIFFSetFieldS2(TIFF* tif, unsigned tag, int param1, int param2);
int TIFFSetFieldS3(TIFF* tif, unsigned tag, int param1, int param2, int param3);

int TIFFSetFieldUShort1(TIFF* tif, unsigned tag, unsigned short param);
int TIFFSetFieldUShort2(TIFF* tif, unsigned tag, unsigned short param1,
                        unsigned short param2);
int TIFFSetFieldUShort3(TIFF* tif, unsigned tag, unsigned short param1,
                        unsigned short param2, unsigned short param3);

int TIFFSetFieldSShort1(TIFF* tif, unsigned tag, short param);
int TIFFSetFieldSShort2(TIFF* tif, unsigned tag, short param1, short param2);
int TIFFSetFieldSShort3(TIFF* tif, unsigned tag, short param1, short param2,
                        short param3);

int TIFFSetFieldULLong1(TIFF* tif, unsigned tag, unsigned long long param);
int TIFFSetFieldULLong2(TIFF* tif, unsigned tag, unsigned long long param1,
                        unsigned long long param2);
int TIFFSetFieldULLong3(TIFF* tif, unsigned tag, unsigned long long param1,
                        unsigned long long param2, unsigned long long param3);

int TIFFSetFieldSLLong1(TIFF* tif, unsigned tag, long long param);
int TIFFSetFieldSLLong2(TIFF* tif, unsigned tag, long long param1,
                        long long param2);
int TIFFSetFieldSLLong3(TIFF* tif, unsigned tag, long long param1,
                        long long param2, long long param3);

int TIFFSetFieldFloat1(TIFF* tif, unsigned tag, float param);
int TIFFSetFieldFloat2(TIFF* tif, unsigned tag, float param1, float param2);
int TIFFSetFieldFloat3(TIFF* tif, unsigned tag, float param1, float param2,
                       float param3);

int TIFFSetFieldDouble1(TIFF* tif, unsigned tag, double param);
int TIFFSetFieldDouble2(TIFF* tif, unsigned tag, double param1, double param2);
int TIFFSetFieldDouble3(TIFF* tif, unsigned tag, double param1, double param2,
                        double param3);
}

#endif
