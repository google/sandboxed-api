#include "guetzli/jpeg_data_reader.h"
#include "guetzli/quality.h"
#include "guetzli_entry_points.h"
#include "png.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/util/statusor.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <stdio.h>
#include <sys/stat.h>
#include <vector>

namespace {

constexpr int kBytesPerPixel = 350;
constexpr int kLowestMemusageMB = 100; // in MB

struct GuetzliInitData {
  std::string in_data;
  guetzli::Params params;
  guetzli::ProcessStats stats;
};

template<typename T>
void CopyMemoryToLenVal(const T* data, size_t size, 
                        sapi::LenValStruct* out_data) {
  free(out_data->data); // Not sure about this
  out_data->size = size;
  T* new_out = static_cast<T*>(malloc(size));
  memcpy(new_out, data, size);
  out_data->data = new_out;
}

sapi::StatusOr<std::string> ReadFromFd(int fd) {
  struct stat file_data;
  auto status = fstat(fd, &file_data);
  
  if (status < 0) {
    return absl::FailedPreconditionError(
      "Error reading input from fd"
    );
  }
  
  auto fsize = file_data.st_size;

  std::unique_ptr<char[]> buf(new char[fsize]);
  status = read(fd, buf.get(), fsize);

  if (status < 0) {
    lseek(fd, 0, SEEK_SET);  
    return absl::FailedPreconditionError(
      "Error reading input from fd"
    );
  }

  return std::string(buf.get(), fsize);
}

sapi::StatusOr<GuetzliInitData> PrepareDataForProcessing(
                                const ProcessingParams* processing_params) {
  auto input_status = ReadFromFd(processing_params->remote_fd);

  if (!input_status.ok()) {
    return input_status.status();
  }

  guetzli::Params guetzli_params;
  guetzli_params.butteraugli_target = static_cast<float>(
    guetzli::ButteraugliScoreForQuality(processing_params->quality));
  
  guetzli::ProcessStats stats;

  if (processing_params->verbose) {
    stats.debug_output_file = stderr;
  }

  return GuetzliInitData{
    std::move(input_status.value()),
    guetzli_params,
    stats
  };
}

inline uint8_t BlendOnBlack(const uint8_t val, const uint8_t alpha) {
  return (static_cast<int>(val) * static_cast<int>(alpha) + 128) / 255;
}

bool ReadPNG(const std::string& data, int* xsize, int* ysize,
             std::vector<uint8_t>* rgb) {
  png_structp png_ptr =
      png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png_ptr) {
    return false;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_read_struct(&png_ptr, nullptr, nullptr);
    return false;
  }

  if (setjmp(png_jmpbuf(png_ptr)) != 0) {
    // Ok we are here because of the setjmp.
    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return false;
  }

  std::istringstream memstream(data, std::ios::in | std::ios::binary);
  png_set_read_fn(png_ptr, static_cast<void*>(&memstream), [](png_structp png_ptr, png_bytep outBytes, png_size_t byteCountToRead) {
    std::istringstream& memstream = *static_cast<std::istringstream*>(png_get_io_ptr(png_ptr));
    
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

  *xsize = png_get_image_width(png_ptr, info_ptr);
  *ysize = png_get_image_height(png_ptr, info_ptr);
  rgb->resize(3 * (*xsize) * (*ysize));

  const int components = png_get_channels(png_ptr, info_ptr);
  switch (components) {
    case 1: {
      // GRAYSCALE
      for (int y = 0; y < *ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &(*rgb)[3 * y * (*xsize)];
        for (int x = 0; x < *xsize; ++x) {
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
      for (int y = 0; y < *ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &(*rgb)[3 * y * (*xsize)];
        for (int x = 0; x < *xsize; ++x) {
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
      for (int y = 0; y < *ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &(*rgb)[3 * y * (*xsize)];
        memcpy(row_out, row_in, 3 * (*xsize));
      }
      break;
    }
    case 4: {
      // RGBA
      for (int y = 0; y < *ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &(*rgb)[3 * y * (*xsize)];
        for (int x = 0; x < *xsize; ++x) {
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
      return false;
  }
  png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
  return true;
}

bool CheckMemoryLimitExceeded(int memlimit_mb, int xsize, int ysize) {
    double pixels = static_cast<double>(xsize) * ysize;
    return memlimit_mb != -1
        && (pixels * kBytesPerPixel / (1 << 20) > memlimit_mb
            || memlimit_mb < kLowestMemusageMB);
}

} // namespace

extern "C" bool ProcessJpeg(const ProcessingParams* processing_params, 
                            sapi::LenValStruct* output) {
  auto processing_data_status = PrepareDataForProcessing(processing_params);

  if (!processing_data_status.status().ok()) {
    fprintf(stderr, "%s\n", processing_data_status.status().ToString().c_str());
    return false;
  }

  guetzli::JPEGData jpg_header;
  if (!guetzli::ReadJpeg(processing_data_status.value().in_data,
      guetzli::JPEG_READ_HEADER, &jpg_header)) {
    fprintf(stderr, "Error reading JPG data from input file\n");
    return false;
  }

  if (CheckMemoryLimitExceeded(processing_params->memlimit_mb, 
      jpg_header.width, jpg_header.height)) {
    fprintf(stderr, "Memory limit would be exceeded.\n");
    return false;
  }

  std::string out_data;
  if (!guetzli::Process(processing_data_status.value().params,
                        &processing_data_status.value().stats,
                        processing_data_status.value().in_data,
                        &out_data)) {
    fprintf(stderr, "Guezli processing failed\n");
    return false;
  }

  CopyMemoryToLenVal(out_data.data(), out_data.size(), output);
  return true;
}

extern "C" bool ProcessRgb(const ProcessingParams* processing_params, 
                          sapi::LenValStruct* output) {
  auto processing_data_status = PrepareDataForProcessing(processing_params);

  if (!processing_data_status.status().ok()) {
    fprintf(stderr, "%s\n", processing_data_status.status().ToString().c_str());
    return false;
  }

  int xsize, ysize;
  std::vector<uint8_t> rgb;

  if (!ReadPNG(processing_data_status.value().in_data, &xsize, &ysize, &rgb)) {
    fprintf(stderr, "Error reading PNG data from input file\n");
    return false;
  }

  if (CheckMemoryLimitExceeded(processing_params->memlimit_mb, xsize, ysize)) {
    fprintf(stderr, "Memory limit would be exceeded.\n");
    return false;
  }

  std::string out_data;
  if (!guetzli::Process(processing_data_status.value().params, 
                        &processing_data_status.value().stats, 
                        rgb, xsize, ysize, &out_data)) {
      fprintf(stderr, "Guetzli processing failed\n");
      return false;
  }

  CopyMemoryToLenVal(out_data.data(), out_data.size(), output);
  return true;
}

extern "C" bool WriteDataToFd(int fd, sapi::LenValStruct* data) {
  return sandbox2::file_util::fileops::WriteToFD(fd, 
    static_cast<const char*>(data->data), data->size);
}