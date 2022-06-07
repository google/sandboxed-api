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

#ifndef CONTRIB_LIBTIFF_WRAPPER_FUNC_H_
#define CONTRIB_LIBTIFF_WRAPPER_FUNC_H_

#include <cstdint>

#include "tiffio.h"  // NOLINT(build/include)

// s - signed
// u - uint32_t
// wrapper for variadic functions TIFFGetField and TIFFSetField

extern "C" {

int TIFFGetField1(TIFF* tif, uint32_t tag, void* param);
int TIFFGetField2(TIFF* tif, uint32_t tag, void* param1, void* param2);
int TIFFGetField3(TIFF* tif, uint32_t tag, void* param1, void* param2,
                  void* param3);

int TIFFSetFieldUChar1(TIFF* tif, uint32_t tag, uint8_t param);
int TIFFSetFieldUChar2(TIFF* tif, uint32_t tag, uint8_t param1, uint8_t param2);
int TIFFSetFieldUChar3(TIFF* tif, uint32_t tag, uint8_t param1, uint8_t param2,
                       uint8_t param3);

int TIFFSetFieldSChar1(TIFF* tif, uint32_t tag, int8_t param);
int TIFFSetFieldSChar2(TIFF* tif, uint32_t tag, int8_t param1, int8_t param2);
int TIFFSetFieldSChar3(TIFF* tif, uint32_t tag, int8_t param1, int8_t param2,
                       int8_t param3);

int TIFFSetFieldU1(TIFF* tif, uint32_t tag, uint32_t param);
int TIFFSetFieldU2(TIFF* tif, uint32_t tag, uint32_t param1, uint32_t param2);
int TIFFSetFieldU3(TIFF* tif, uint32_t tag, uint32_t param1, uint32_t param2,
                   uint32_t param3);

int TIFFSetFieldS1(TIFF* tif, uint32_t tag, int param);
int TIFFSetFieldS2(TIFF* tif, uint32_t tag, int param1, int param2);
int TIFFSetFieldS3(TIFF* tif, uint32_t tag, int param1, int param2, int param3);

int TIFFSetFieldUShort1(TIFF* tif, uint32_t tag, uint16_t param);
int TIFFSetFieldUShort2(TIFF* tif, uint32_t tag, uint16_t param1,
                        uint16_t param2);
int TIFFSetFieldUShort3(TIFF* tif, uint32_t tag, uint16_t param1,
                        uint16_t param2, uint16_t param3);

int TIFFSetFieldSShort1(TIFF* tif, uint32_t tag, int16_t param);
int TIFFSetFieldSShort2(TIFF* tif, uint32_t tag, int16_t param1,
                        int16_t param2);
int TIFFSetFieldSShort3(TIFF* tif, uint32_t tag, int16_t param1, int16_t param2,
                        int16_t param3);

int TIFFSetFieldULLong1(TIFF* tif, uint32_t tag, uint64_t param);
int TIFFSetFieldULLong2(TIFF* tif, uint32_t tag, uint64_t param1,
                        uint64_t param2);
int TIFFSetFieldULLong3(TIFF* tif, uint32_t tag, uint64_t param1,
                        uint64_t param2, uint64_t param3);

int TIFFSetFieldSLLong1(TIFF* tif, uint32_t tag, int64_t param);
int TIFFSetFieldSLLong2(TIFF* tif, uint32_t tag, int64_t param1,
                        int64_t param2);
int TIFFSetFieldSLLong3(TIFF* tif, uint32_t tag, int64_t param1, int64_t param2,
                        int64_t param3);

int TIFFSetFieldFloat1(TIFF* tif, uint32_t tag, float param);
int TIFFSetFieldFloat2(TIFF* tif, uint32_t tag, float param1, float param2);
int TIFFSetFieldFloat3(TIFF* tif, uint32_t tag, float param1, float param2,
                       float param3);

int TIFFSetFieldDouble1(TIFF* tif, uint32_t tag, double param);
int TIFFSetFieldDouble2(TIFF* tif, uint32_t tag, double param1, double param2);
int TIFFSetFieldDouble3(TIFF* tif, uint32_t tag, double param1, double param2,
                        double param3);

}  // extern "C"

#endif  // CONTRIB_LIBTIFF_WRAPPER_FUNC_H_
