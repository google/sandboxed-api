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

#include "guetzli_entry_points.h"  // NOLINT(build/include)

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "guetzli/jpeg_data_reader.h"
#include "guetzli/quality.h"
#include "png.h"  // NOLINT(build/include)
#include "absl/status/statusor.h"
#include "sandboxed_api/util/fileops.h"

namespace {

constexpr int kBytesPerPixel = 350;
constexpr int kLowestMemusageMB = 100;

struct GuetzliInitData {
  std::string in_data;
  guetzli::Params params;
  guetzli::ProcessStats stats;
};

struct ImageData {
  int xsize;
  int ysize;
  std::vector<uint8_t> rgb;
};

sapi::LenValStruct CreateLenValFromData(const void* data, size_t size) {
  void* new_data = malloc(size);
  memcpy(new_data, data, size);
  return {size, new_data};
}

absl::StatusOr<std::string> ReadFromFd(int fd) {
  struct stat file_data;
  int status = fstat(fd, &file_data);

  if (status < 0) {
    return absl::FailedPreconditionError("Error reading input from fd");
  }

  std::string result;
  result.resize(file_data.st_size);
  status = read(fd, result.data(), result.size());

  if (status < 0) {
    return absl::FailedPreconditionError("Error reading input from fd");
  }

  return result;
}

absl::StatusOr<GuetzliInitData> PrepareDataForProcessing(
    const ProcessingParams& processing_params) {
  absl::StatusOr<std::string> input = ReadFromFd(processing_params.remote_fd);

  if (!input.ok()) {
    return input.status();
  }

  guetzli::Params guetzli_params;
  guetzli_params.butteraugli_target = static_cast<float>(
      guetzli::ButteraugliScoreForQuality(processing_params.quality));

  guetzli::ProcessStats stats;

  if (processing_params.verbose) {
    stats.debug_output_file = stderr;
  }

  return GuetzliInitData{std::move(input.value()), guetzli_params, stats};
}

inline uint8_t BlendOnBlack(const uint8_t val, const uint8_t alpha) {
  return (static_cast<int>(val) * static_cast<int>(alpha) + 128) / 255;
}

// Modified version of ReadPNG from original guetzli.cc
absl::StatusOr<ImageData> ReadPNG(const std::string& data) {
  std::vector<uint8_t> rgb;
  int xsize, ysize;
  png_structp png_ptr =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_ptr) {
    return absl::FailedPreconditionError(
        "Error reading PNG data from input file");
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    return absl::FailedPreconditionError(
        "Error reading PNG data from input file");
  }

  if (setjmp(png_jmpbuf(png_ptr)) != 0) {
    // Ok we are here because of the setjmp.
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return absl::FailedPreconditionError(
        "Error reading PNG data from input file");
  }

  std::istringstream memstream(data, std::ios::in | std::ios::binary);
  png_set_read_fn(
      png_ptr, static_cast<void*>(&memstream),
      [](png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead) {
        std::istringstream& memstream =
            *static_cast<std::istringstream*>(png_get_io_ptr(png_ptr));

        memstream.read(reinterpret_cast<char*>(outBytes), byteCountToRead);

        if (memstream.eof()) png_error(png_ptr, "unexpected end of data");
        if (memstream.fail()) png_error(png_ptr, "read from memory error");
      });

  // The png_transforms flags are as follows:
  // packing == convert 1,2,4 bit images,
  // strip == 16 -> 8 bits / channel,
  // shift == use sBIT dynamics, and
  // expand == palettes -> rgb, grayscale -> 8 bit images, tRNS -> alpha.
  const unsigned int png_transforms =
      PNG_TRANSFORM_PACKING | PNG_TRANSFORM_EXPAND | PNG_TRANSFORM_STRIP_16;

  png_read_png(png_ptr, info_ptr, png_transforms, nullptr);

  png_bytep* row_pointers = png_get_rows(png_ptr, info_ptr);

  xsize = png_get_image_width(png_ptr, info_ptr);
  ysize = png_get_image_height(png_ptr, info_ptr);
  rgb.resize(3 * xsize * ysize);

  const int components = png_get_channels(png_ptr, info_ptr);
  switch (components) {
    case 1: {
      // GRAYSCALE
      for (int y = 0; y < ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &rgb[3 * y * xsize];
        for (int x = 0; x < xsize; ++x) {
          const uint8_t gray = row_in[x];
          row_out[3 * x + 0] = gray;
          row_out[3 * x + 1] = gray;
          row_out[3 * x + 2] = gray;
        }
      }
      break;
    }
    case 2: {
      // GRAYSCALE + ALPHA
      for (int y = 0; y < ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &rgb[3 * y * xsize];
        for (int x = 0; x < xsize; ++x) {
          const uint8_t gray = BlendOnBlack(row_in[2 * x], row_in[2 * x + 1]);
          row_out[3 * x + 0] = gray;
          row_out[3 * x + 1] = gray;
          row_out[3 * x + 2] = gray;
        }
      }
      break;
    }
    case 3: {
      // RGB
      for (int y = 0; y < ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &rgb[3 * y * xsize];
        memcpy(row_out, row_in, 3 * xsize);
      }
      break;
    }
    case 4: {
      // RGBA
      for (int y = 0; y < ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &rgb[3 * y * xsize];
        for (int x = 0; x < xsize; ++x) {
          const uint8_t alpha = row_in[4 * x + 3];
          row_out[3 * x + 0] = BlendOnBlack(row_in[4 * x + 0], alpha);
          row_out[3 * x + 1] = BlendOnBlack(row_in[4 * x + 1], alpha);
          row_out[3 * x + 2] = BlendOnBlack(row_in[4 * x + 2], alpha);
        }
      }
      break;
    }
    default:
      png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
      return absl::FailedPreconditionError(
          "Error reading PNG data from input file");
  }
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

  return ImageData{xsize, ysize, std::move(rgb)};
}

bool CheckMemoryLimitExceeded(int memlimit_mb, int xsize, int ysize) {
  double pixels = static_cast<double>(xsize) * ysize;
  return memlimit_mb != -1 &&
         (pixels * kBytesPerPixel / (1 << 20) > memlimit_mb ||
          memlimit_mb < kLowestMemusageMB);
}

}  // namespace

extern "C" bool ProcessJpeg(const ProcessingParams* processing_params,
                            sapi::LenValStruct* output) {
  auto processing_data = PrepareDataForProcessing(*processing_params);

  if (!processing_data.ok()) {
    std::cerr << processing_data.status().ToString() << std::endl;
    return false;
  }

  guetzli::JPEGData jpg_header;
  if (!guetzli::ReadJpeg(processing_data->in_data, guetzli::JPEG_READ_HEADER,
                         &jpg_header)) {
    std::cerr << "Error reading JPG data from input file" << std::endl;
    return false;
  }

  if (CheckMemoryLimitExceeded(processing_params->memlimit_mb, jpg_header.width,
                               jpg_header.height)) {
    std::cerr << "Memory limit would be exceeded" << std::endl;
    return false;
  }

  std::string out_data;
  if (!guetzli::Process(processing_data->params, &processing_data->stats,
                        processing_data->in_data, &out_data)) {
    std::cerr << "Guezli processing failed" << std::endl;
    return false;
  }

  *output = CreateLenValFromData(out_data.data(), out_data.size());
  return true;
}

extern "C" bool ProcessRgb(const ProcessingParams* processing_params,
                           sapi::LenValStruct* output) {
  auto processing_data = PrepareDataForProcessing(*processing_params);

  if (!processing_data.ok()) {
    std::cerr << processing_data.status().ToString() << std::endl;
    return false;
  }

  auto png_data = ReadPNG(processing_data->in_data);
  if (!png_data.ok()) {
    std::cerr << "Error reading PNG data from input file" << std::endl;
    return false;
  }

  if (CheckMemoryLimitExceeded(processing_params->memlimit_mb, png_data->xsize,
                               png_data->ysize)) {
    std::cerr << "Memory limit would be exceeded" << std::endl;
    return false;
  }

  std::string out_data;
  if (!guetzli::Process(processing_data->params, &processing_data->stats,
                        png_data->rgb, png_data->xsize, png_data->ysize,
                        &out_data)) {
    std::cerr << "Guetzli processing failed" << std::endl;
    return false;
  }

  *output = CreateLenValFromData(out_data.data(), out_data.size());
  return true;
}

extern "C" bool WriteDataToFd(int fd, sapi::LenValStruct* data) {
  return sandbox2::file_util::fileops::WriteToFD(
      fd, static_cast<const char*>(data->data), data->size);
}
