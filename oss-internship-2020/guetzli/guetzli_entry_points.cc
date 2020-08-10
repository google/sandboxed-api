#include "guetzli/jpeg_data_reader.h"
#include "guetzli/quality.h"
#include "guetzli_entry_points.h"
#include "png.h"
#include "sandboxed_api/sandbox2/util/fileops.h"

#include <algorithm>
#include <iostream>
#include <stdio.h>
#include <sys/stat.h>
#include <vector>

namespace {

inline uint8_t BlendOnBlack(const uint8_t val, const uint8_t alpha) {
  return (static_cast<int>(val) * static_cast<int>(alpha) + 128) / 255;
}

template<typename T>
void CopyMemoryToLenVal(const T* data, size_t size, 
                        sapi::LenValStruct* out_data) {
  free(out_data->data); // Not sure about this
  out_data->size = size;
  T* new_out = static_cast<T*>(malloc(size));
  memcpy(new_out, data, size);
  out_data->data = new_out;
}

} // namespace

extern "C" bool ProcessJPEGString(const guetzli::Params* params,
                                  int verbose,
                                  sapi::LenValStruct* in_data, 
                                  sapi::LenValStruct* out_data)
{
  std::string in_data_temp(static_cast<const char*>(in_data->data), 
      in_data->size);

  guetzli::ProcessStats stats;
  if (verbose > 0) {
    stats.debug_output_file = stderr;
  }

  std::string temp_out = "";
  auto result = guetzli::Process(*params, &stats, in_data_temp, &temp_out);

  if (result) {
    CopyMemoryToLenVal(temp_out.data(), temp_out.size(), out_data);
  }

  return result;
}

extern "C" bool ProcessRGBData(const guetzli::Params* params,
                                int verbose,
                                sapi::LenValStruct* rgb, 
                                int w, int h,
                                sapi::LenValStruct* out_data)
{
  std::vector<uint8_t> in_data_temp;
  in_data_temp.reserve(rgb->size);

  auto* rgb_data = static_cast<uint8_t*>(rgb->data);
  std::copy(rgb_data, rgb_data + rgb->size, std::back_inserter(in_data_temp));

  guetzli::ProcessStats stats;
  if (verbose > 0) {
    stats.debug_output_file = stderr;
  }

  std::string temp_out = "";
  auto result = 
      guetzli::Process(*params, &stats, in_data_temp, w, h, &temp_out);

  //TODO: Move shared part of the code to another function
  if (result) {
    CopyMemoryToLenVal(temp_out.data(), temp_out.size(), out_data);
  }

  return result;
}

extern "C" bool ReadPng(sapi::LenValStruct* in_data, 
                        int* xsize, int* ysize,
                        sapi::LenValStruct* rgb_out)
{
  std::string data(static_cast<const char*>(in_data->data), in_data->size);
  std::vector<uint8_t> rgb;
  
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
  rgb.resize(3 * (*xsize) * (*ysize));

  const int components = png_get_channels(png_ptr, info_ptr);
  switch (components) {
    case 1: {
      // GRAYSCALE
      for (int y = 0; y < *ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &(rgb)[3 * y * (*xsize)];
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
        uint8_t* row_out = &(rgb)[3 * y * (*xsize)];
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
        uint8_t* row_out = &(rgb)[3 * y * (*xsize)];
        memcpy(row_out, row_in, 3 * (*xsize));
      }
      break;
    }
    case 4: {
      // RGBA
      for (int y = 0; y < *ysize; ++y) {
        const uint8_t* row_in = row_pointers[y];
        uint8_t* row_out = &(rgb)[3 * y * (*xsize)];
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

  CopyMemoryToLenVal(rgb.data(), rgb.size(), rgb_out);

  return true;
}

extern "C" bool ReadJpegData(sapi::LenValStruct* in_data, 
                              int mode,
                              int* xsize, int* ysize)
{
  std::string data(static_cast<const char*>(in_data->data), in_data->size);
  guetzli::JPEGData jpg;

  auto result = guetzli::ReadJpeg(data, 
    static_cast<guetzli::JpegReadMode>(mode), &jpg);

  if (result) {
    *xsize = jpg.width;
    *ysize = jpg.height;
  }

  return result;
}

extern "C" double ButteraugliScoreQuality(double quality) {
  return guetzli::ButteraugliScoreForQuality(quality);
}

extern "C" bool ReadDataFromFd(int fd, sapi::LenValStruct* out_data) {
  struct stat file_data;
  auto status = fstat(fd, &file_data);
  
  if (status < 0) {
    return false;
  }
  
  auto fsize = file_data.st_size;

  std::unique_ptr<char[]> buf(new char[fsize]);
  status = read(fd, buf.get(), fsize);

  if (status < 0) {
    return false;
  }
  
  CopyMemoryToLenVal(buf.get(), fsize, out_data);

  return true;
}

extern "C" bool WriteDataToFd(int fd, sapi::LenValStruct* data) {
  return sandbox2::file_util::fileops::WriteToFD(fd, 
    static_cast<const char*>(data->data), data->size);
}