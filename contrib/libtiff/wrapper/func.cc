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

#include "contrib/libtiff/wrapper/func.h"

#include <cstdint>

// Work around the linker not including this symbol in the final sandboxee
// binary.
static volatile auto unused_reference_function =
    reinterpret_cast<uintptr_t>(&TIFFReadRGBATile);

int TIFFGetField1(TIFF* tif, uint32_t tag, void* param) {
  return TIFFGetField(tif, tag, param);
}

int TIFFGetField2(TIFF* tif, uint32_t tag, void* param1, void* param2) {
  return TIFFGetField(tif, tag, param1, param2);
}

int TIFFGetField3(TIFF* tif, uint32_t tag, void* param1, void* param2,
                  void* param3) {
  return TIFFGetField(tif, tag, param1, param2, param3);
}

int TIFFSetFieldUChar1(TIFF* tif, uint32_t tag, uint8_t param) {
  return TIFFSetField(tif, tag, param);
}

int TIFFSetFieldUChar2(TIFF* tif, uint32_t tag, uint8_t param1,
                       uint8_t param2) {
  return TIFFSetField(tif, tag, param1, param2);
}

int TIFFSetFieldUChar3(TIFF* tif, uint32_t tag, uint8_t param1, uint8_t param2,
                       uint8_t param3) {
  return TIFFSetField(tif, tag, param1, param2, param3);
}

int TIFFSetFieldSChar1(TIFF* tif, uint32_t tag, int8_t param) {
  return TIFFSetField(tif, tag, param);
}

int TIFFSetFieldSChar2(TIFF* tif, uint32_t tag, int8_t param1, int8_t param2) {
  return TIFFSetField(tif, tag, param1, param2);
}

int TIFFSetFieldSChar3(TIFF* tif, uint32_t tag, int8_t param1, int8_t param2,
                       int8_t param3) {
  return TIFFSetField(tif, tag, param1, param2, param3);
}

int TIFFSetFieldU1(TIFF* tif, uint32_t tag, uint32_t param) {
  return TIFFSetField(tif, tag, param);
}

int TIFFSetFieldU2(TIFF* tif, uint32_t tag, uint32_t param1, uint32_t param2) {
  return TIFFSetField(tif, tag, param1, param2);
}

int TIFFSetFieldU3(TIFF* tif, uint32_t tag, uint32_t param1, uint32_t param2,
                   uint32_t param3) {
  return TIFFSetField(tif, tag, param1, param2, param3);
}

int TIFFSetFieldS1(TIFF* tif, uint32_t tag, int param) {
  return TIFFSetField(tif, tag, param);
}

int TIFFSetFieldS2(TIFF* tif, uint32_t tag, int param1, int param2) {
  return TIFFSetField(tif, tag, param1, param2);
}

int TIFFSetFieldS3(TIFF* tif, uint32_t tag, int param1, int param2,
                   int param3) {
  return TIFFSetField(tif, tag, param1, param2, param3);
}

int TIFFSetFieldUShort1(TIFF* tif, uint32_t tag, uint16_t param) {
  return TIFFSetField(tif, tag, param);
}

int TIFFSetFieldUShort2(TIFF* tif, uint32_t tag, uint16_t param1,
                        uint16_t param2) {
  return TIFFSetField(tif, tag, param1, param2);
}

int TIFFSetFieldUShort3(TIFF* tif, uint32_t tag, uint16_t param1,
                        uint16_t param2, uint16_t param3) {
  return TIFFSetField(tif, tag, param1, param2, param3);
}

int TIFFSetFieldSShort1(TIFF* tif, uint32_t tag, int16_t param) {
  return TIFFSetField(tif, tag, param);
}

int TIFFSetFieldSShort2(TIFF* tif, uint32_t tag, int16_t param1,
                        int16_t param2) {
  return TIFFSetField(tif, tag, param1, param2);
}

int TIFFSetFieldSShort3(TIFF* tif, uint32_t tag, int16_t param1, int16_t param2,
                        int16_t param3) {
  return TIFFSetField(tif, tag, param1, param2, param3);
}

int TIFFSetFieldULLong1(TIFF* tif, uint32_t tag, uint64_t param) {
  return TIFFSetField(tif, tag, param);
}

int TIFFSetFieldULLong2(TIFF* tif, uint32_t tag, uint64_t param1,
                        uint64_t param2) {
  return TIFFSetField(tif, tag, param1, param2);
}

int TIFFSetFieldULLong3(TIFF* tif, uint32_t tag, uint64_t param1,
                        uint64_t param2, uint64_t param3) {
  return TIFFSetField(tif, tag, param1, param2, param3);
}

int TIFFSetFieldSLLong1(TIFF* tif, uint32_t tag, int64_t param) {
  return TIFFSetField(tif, tag, param);
}

int TIFFSetFieldSLLong2(TIFF* tif, uint32_t tag, int64_t param1,
                        int64_t param2) {
  return TIFFSetField(tif, tag, param1, param2);
}

int TIFFSetFieldSLLong3(TIFF* tif, uint32_t tag, int64_t param1, int64_t param2,
                        int64_t param3) {
  return TIFFSetField(tif, tag, param1, param2, param3);
}

int TIFFSetFieldFloat1(TIFF* tif, uint32_t tag, float param) {
  return TIFFSetField(tif, tag, param);
}

int TIFFSetFieldFloat2(TIFF* tif, uint32_t tag, float param1, float param2) {
  return TIFFSetField(tif, tag, param1, param2);
}

int TIFFSetFieldFloat3(TIFF* tif, uint32_t tag, float param1, float param2,
                       float param3) {
  return TIFFSetField(tif, tag, param1, param2, param3);
}

int TIFFSetFieldDouble1(TIFF* tif, uint32_t tag, double param) {
  return TIFFSetField(tif, tag, param);
}

int TIFFSetFieldDouble2(TIFF* tif, uint32_t tag, double param1, double param2) {
  return TIFFSetField(tif, tag, param1, param2);
}

int TIFFSetFieldDouble3(TIFF* tif, uint32_t tag, double param1, double param2,
                        double param3) {
  return TIFFSetField(tif, tag, param1, param2, param3);
}
