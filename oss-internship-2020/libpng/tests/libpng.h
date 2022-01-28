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

#ifndef LIBPNG_TESTS_LIBPNG_H_
#define LIBPNG_TESTS_LIBPNG_H_

// Defines from libpng library. The problem is that the build throws the error
// "Duplicate functions" if #include <png.h> is added.

#define PNG_FORMAT_FLAG_ALPHA 0x01U
#define PNG_FORMAT_FLAG_COLOR 0x02U
#define PNG_FORMAT_FLAG_LINEAR 0x04U
#define PNG_FORMAT_FLAG_COLORMAP 0x08U

#ifdef PNG_FORMAT_BGR_SUPPORTED
#define PNG_FORMAT_FLAG_BGR 0x10U
#endif

#ifdef PNG_FORMAT_AFIRST_SUPPORTED
#define PNG_FORMAT_FLAG_AFIRST 0x20U
#endif

#define PNG_FORMAT_FLAG_ASSOCIATED_ALPHA 0x40U

#define PNG_FORMAT_GRAY 0
#define PNG_FORMAT_GA PNG_FORMAT_FLAG_ALPHA
#define PNG_FORMAT_AG (PNG_FORMAT_GA | PNG_FORMAT_FLAG_AFIRST)
#define PNG_FORMAT_RGB PNG_FORMAT_FLAG_COLOR
#define PNG_FORMAT_BGR (PNG_FORMAT_FLAG_COLOR | PNG_FORMAT_FLAG_BGR)
#define PNG_FORMAT_RGBA (PNG_FORMAT_RGB | PNG_FORMAT_FLAG_ALPHA)
#define PNG_FORMAT_ARGB (PNG_FORMAT_RGBA | PNG_FORMAT_FLAG_AFIRST)
#define PNG_FORMAT_BGRA (PNG_FORMAT_BGR | PNG_FORMAT_FLAG_ALPHA)
#define PNG_FORMAT_ABGR (PNG_FORMAT_BGRA | PNG_FORMAT_FLAG_AFIRST)

#define PNG_IMAGE_VERSION 1

#define PNG_IMAGE_PIXEL_(test, fmt) \
  (((fmt)&PNG_FORMAT_FLAG_COLORMAP) ? 1 : test(fmt))

#define PNG_IMAGE_SAMPLE_CHANNELS(fmt) \
  (((fmt) & (PNG_FORMAT_FLAG_COLOR | PNG_FORMAT_FLAG_ALPHA)) + 1)

#define PNG_IMAGE_PIXEL_CHANNELS(fmt) \
  PNG_IMAGE_PIXEL_(PNG_IMAGE_SAMPLE_CHANNELS, fmt)

#define PNG_IMAGE_ROW_STRIDE(image) \
  (PNG_IMAGE_PIXEL_CHANNELS((image).format) * (image).width)

#define PNG_IMAGE_SAMPLE_COMPONENT_SIZE(fmt) \
  ((((fmt)&PNG_FORMAT_FLAG_LINEAR) >> 2) + 1)

#define PNG_IMAGE_PIXEL_COMPONENT_SIZE(fmt) \
  PNG_IMAGE_PIXEL_(PNG_IMAGE_SAMPLE_COMPONENT_SIZE, fmt)

#define PNG_IMAGE_BUFFER_SIZE(image, row_stride)                     \
  (PNG_IMAGE_PIXEL_COMPONENT_SIZE((image).format) * (image).height * \
   (row_stride))

#define PNG_IMAGE_SIZE(image) \
  PNG_IMAGE_BUFFER_SIZE(image, PNG_IMAGE_ROW_STRIDE(image))

typedef uint8_t *png_bytep;

#if UINT_MAX == 65535
typedef unsigned int png_uint_16;
#elif USHRT_MAX == 65535
typedef unsigned short png_uint_16;  // NOLINT(runtime/int)
#else
#error "libpng requires an unsigned 16-bit type"
#endif

#define PNG_LIBPNG_VER_STRING "1.6.38.git"

#define PNG_COLOR_MASK_COLOR 2
#define PNG_COLOR_MASK_ALPHA 4

#define PNG_COLOR_TYPE_RGB (PNG_COLOR_MASK_COLOR)
#define PNG_COLOR_TYPE_RGB_ALPHA (PNG_COLOR_MASK_COLOR | PNG_COLOR_MASK_ALPHA)
#define PNG_COLOR_TYPE_RGBA PNG_COLOR_TYPE_RGB_ALPHA

#define PNG_FILTER_TYPE_BASE 0
#define PNG_COMPRESSION_TYPE_BASE 0
#define PNG_INTERLACE_NONE 0

#endif  // LIBPNG_TESTS_LIBPNG_H_
