// !! think that it can't be ended


#include <stdlib.h>
#include <stdio.h>
#include <cstdlib>
#include <setjmp.h> /* required for error handling */

#include "../sandboxed.h"
#include "libpng.h"

static unsigned int
component(png_const_bytep row, png_uint_32 x, unsigned int c,
  unsigned int bit_depth, unsigned int channels)
{
  png_uint_32 bit_offset_hi = bit_depth * ((x >> 6) * channels);
  png_uint_32 bit_offset_lo = bit_depth * ((x & 0x3f) * channels + c);

  row = (png_const_bytep)(((const png_byte (*)[8])row) + bit_offset_hi);
  row += bit_offset_lo >> 3;
  bit_offset_lo &= 0x07;

  switch (bit_depth)
  {
    case 1: return (row[0] >> (7-bit_offset_lo)) & 0x01;
    case 2: return (row[0] >> (6-bit_offset_lo)) & 0x03;
    case 4: return (row[0] >> (4-bit_offset_lo)) & 0x0f;
    case 8: return row[0];
    case 16: return (row[0] << 8) + row[1];
    default:
        fprintf(stderr, "pngpixel: invalid bit depth %u\n", bit_depth);
        exit(1);
  }
}

absl::Status print_pixel(const sapi::v::RemotePtr& png_ptr,
  const sapi::v::RemotePtr& info_ptr, sapi::v::Array<uint8_t>& row, uint32_t x) {

  SAPI_ASSIGN_OR_RETURN(uint32_t bit_depth,
    api.png_get_bit_depth(png_ptr.PtrBoth(), info_ptr.PtrBoth()));

  SAPI_ASSIGN_OR_RETURN(uint8_t color_type,
    api.png_get_color_type(png_ptr.PtrBoth(), info_ptr.PtrBoth()));

  switch (color_type) {
    case PNG_COLOR_TYPE_GRAY:
      std::cout << "GRAY " << component(row, x, 0, bit_depth, 1)) << "\n";
      return absl::OkStatus();

    case PNG_COLOR_TYPE_PALETTE: {
          int index = component(row, x, 0, bit_depth, 1);
          sapi::v::Int num_palette = 0;
          sapi::v::Struct<png_color> palette;

          SAPI_ASSIGN_OR_RETURN(uint8_t plte,
            api.png_get_PLTE(png_ptr.PtrBoth(), info_ptr.PtrBoth(), palette.PtrBoth(), num_palette.PtrBoth()));

          if ((plte & PNG_INFO_PLTE) && num_palette > 0 && palette.GetValue()) {

              png_bytep trans_alpha = NULL;
              int num_trans = 0;
              if ((png_get_tRNS(png_ptr, info_ptr, &trans_alpha, &num_trans,
                NULL) & PNG_INFO_tRNS) && num_trans > 0 &&
                trans_alpha != NULL)
                printf("INDEXED %u = %d %d %d %d\n", index,
                    palette[index].red, palette[index].green,
                    palette[index].blue,
                    index < num_trans ? trans_alpha[index] : 255);

              else /* no transparency */
                printf("INDEXED %u = %d %d %d\n", index,
                    palette[index].red, palette[index].green,
                    palette[index].blue);
          }

          else
              printf("INDEXED %u = invalid index\n", index);
        }
        return;

    case PNG_COLOR_TYPE_RGB:
        printf("RGB %u %u %u\n", component(row, x, 0, bit_depth, 3),
          component(row, x, 1, bit_depth, 3),
          component(row, x, 2, bit_depth, 3));
        return;

    case PNG_COLOR_TYPE_GRAY_ALPHA:
        printf("GRAY+ALPHA %u %u\n", component(row, x, 0, bit_depth, 2),
          component(row, x, 1, bit_depth, 2));
        return;

    case PNG_COLOR_TYPE_RGB_ALPHA:
        printf("RGBA %u %u %u %u\n", component(row, x, 0, bit_depth, 4),
          component(row, x, 1, bit_depth, 4),
          component(row, x, 2, bit_depth, 4),
          component(row, x, 3, bit_depth, 4));
        return;

    default:
        png_error(png_ptr, "pngpixel: invalid color type");
  }
}

absl::Status LibPNGMain(int x, int y, const char *file_path) {
  auto* f = std::fopen(file_path, "rb");

  if (!f) {
    return absl::InternalError(absl::StrCat("Could not open file ", srcfile));
  }

  volatile png_bytep row = NULL; /// ??

  SAPI_ASSIGN_OR_RETURN(
    auto status_or_png_struct, api.png_create_read_struct(PNG_LIBPNG_VER_STRING,
    sapi::v::NullPtr().PtrBoth(), sapi::v::NullPtr().PtrBoth(), sapi::v::NullPtr().PtrBoth()));

  sapi::v::RemotePtr png_ptr(status_or_png_struct.value());
  if (!png_ptr.GetValue()) {
    return absl::InternalError("out of memory allocating png_struct");
  }

  SAPI_ASSIGN_OR_RETURN(
    auto status_or_png_info, api.png_create_info_struct(png_ptr.PtrBoth()));

  sapi::v::RemotePtr info_ptr(status_or_png_info.value());
  if (!info_ptr.GetValue()) {
    SAPI_RETURN_IF_ERROR(api.png_destroy_read_struct(&png_ptr, sapi::v::NullPtr().PtrBoth(), sapi::v::NullPtr().PtrBoth()));
    return absl::InternalError("out of memory allocating png_info");
  }

  api.setjmp(png_jmpbuf(png_ptr.PtrBoth())) /// ??

  sapi::v::UInt width;
  sapi::v::UInt height;

  sapi::v::Int bit_depth;
  sapi::v::Int color_type;
  sapi::v::Int interlace_method;
  sapi::v::Int compression_method;
  sapi::v::Int filter_method;

  if (setjmp(png_jmpbuf(png_ptr)) == 0)

  SAPI_RETURN_IF_ERROR(api.png_init_io(png_ptr.PtrBoth(), f)); /// help me!
  SAPI_RETURN_IF_ERROR(api.png_read_info(png_ptr.PtrBoth(), info_ptr.PtrBoth()));

  SAPI_ASSIGN_OR_RETURN(
    uint32_t row_size, api.png_get_rowbytes(png_ptr.PtrBoth(), info_ptr.PtrBoth()));

  sapi::v::Array<uint8_t> row_tmp(row_size);

  SAPI_ASSIGN_OR_RETURN(auto result,
    api.png_get_IHDR(png_ptr.PtrBoth(), info_ptr.PtrBoth(), width.PtrBoth(), height.PtrBoth(),
    bit_depth.PtrBoth(), color_type.PtrBoth(), interlace_method.PtrBoth(),
    compression_method.PtrBoth(), filter_method.PtrBoth()));
  if (!result) {
    SAPI_RETURN_IF_ERROR(api.png_destroy_info_struct(png_ptr, &info_ptr));
    SAPI_RETURN_IF_ERROR(api.png_destroy_read_struct(&png_ptr, sapi::v::NullPtr().PtrBoth(), sapi::v::NullPtr().PtrBoth()));
    return absl::InternalError("png_get_IHDR failed");
  }

  int passes;

  switch (interlace_method.GetValue()) {
    case PNG_INTERLACE_NONE:
        passes = 1;
        break;
    case PNG_INTERLACE_ADAM7:
        passes = PNG_INTERLACE_ADAM7_PASSES;
        break;
    default:
        SAPI_RETURN_IF_ERROR(api.png_destroy_info_struct(png_ptr.PtrBoth(), &info_ptr));
        SAPI_RETURN_IF_ERROR(api.png_destroy_read_struct(&png_ptr, sapi::v::NullPtr().PtrBoth(), sapi::v::NullPtr().PtrBoth()));
        return absl::InternalError("unknown interlace");
  }

  SAPI_RETURN_IF_ERROR(api.png_start_read_image(png_ptr.PtrBoth(), &info_ptr));
  bool is_data_found = false;

  for (size_t pass = 0; pass < passes && !is_data_found; ++pass) {
    uint32_t ystart;
    uint32_t xstart;
    uint32_t ystep;
    uint32_t xstep;

    if (interlace_method.GetValue() == PNG_INTERLACE_ADAM7) {
      if (PNG_PASS_COLS(width.GetValue(), pass) == 0) {
        continue;
      }

      xstart = PNG_PASS_START_COL(pass);
      ystart = PNG_PASS_START_ROW(pass);
      xstep = PNG_PASS_COL_OFFSET(pass);
      ystep = PNG_PASS_ROW_OFFSET(pass);

    } else {
      ystart = 0;
      xstart = 0;
      ystep = 1;
      xstep = 1;
    }

    for (size_t py = ystart; py < height && !is_data_found; py += ystep) {
      SAPI_RETURN_IF_ERROR(api.png_read_row(png_ptr.PtrBoth(), row_tmp.PtrBoth(), sapi::v::NullPtr().PtrBoth()));

      if (y == py) {
        for (size_t px = xstart, ppx = 0; px < width && !is_data_found; px += xstep, ++ppx) {
          if (x == px) {
            print_pixel(png_ptr, info_ptr, row_tmp, ppx));
            is_data_found = true;
          }
        }
      }
    }
  }

  SAPI_RETURN_IF_ERROR(api.png_destroy_info_struct(png_ptr, &info_ptr));
  SAPI_RETURN_IF_ERROR(api.png_destroy_read_struct(&png_ptr, sapi::v::NullPtr().PtrBoth(), sapi::v::NullPtr().PtrBoth()));

  return absl::OkStatus();
}

int main(int argc, const char **argv) {
  if (argc != 4) {
    LOG(ERROR) << "pngpixel: usage: pngpixel x y png-file";
    return EXIT_FAILURE;
  }

  auto status = LibPNGMain(atoi(argv[1]), atoi(argv[2]), argv[3]);
  if (!status.ok()) {
    LOG(ERROR) << "LibPNGMain failed with error:\n"
                << status.ToString() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
