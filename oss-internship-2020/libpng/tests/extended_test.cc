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

#include <fcntl.h>
#include <unistd.h>

#include "../sandboxed.h"  // NOLINT(build/include)
#include "helper.h"        // NOLINT(build/include)
#include "libpng.h"        // NOLINT(build/include)
#include "gtest/gtest.h"
#include "sandboxed_api/util/status_matchers.h"

namespace {

using ::sapi::IsOk;
using ::testing::ContainerEq;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::IsTrue;
using ::testing::NotNull;

struct Data {
  int width;
  int height;
  uint8_t color_type;
  uint8_t bit_depth;
  int number_of_passes;
  size_t rowbytes;
  std::unique_ptr<sapi::v::Array<uint8_t>> row_pointers;
};

void ReadPng(LibPNGApi& api, absl::string_view infile, Data& data) {
  sapi::v::Fd fd(open(infile.data(), O_RDONLY));

  ASSERT_THAT(fd.GetValue(), Ge(0)) << "Error opening input file";
  ASSERT_THAT((&api)->sandbox()->TransferToSandboxee(&fd), IsOk());

  ASSERT_THAT(fd.GetRemoteFd(), Ge(0)) << "Error receiving remote FD";

  sapi::v::ConstCStr rb_var("rb");
  absl::StatusOr<void*> status_or_file =
      api.png_fdopen(fd.GetRemoteFd(), rb_var.PtrBefore());
  ASSERT_THAT(status_or_file, IsOk());

  sapi::v::RemotePtr file(status_or_file.value());
  ASSERT_THAT(file.GetValue(), NotNull()) << "Could not open " << infile;

  sapi::v::Array<char> header(8);
  ASSERT_THAT(api.png_fread(header.PtrBoth(), 1, header.GetSize(), &file),
              IsOk());

  absl::StatusOr<int> status_or_int =
      api.png_sig_cmp(header.PtrBoth(), 0, header.GetSize());
  ASSERT_THAT(status_or_int, IsOk());
  ASSERT_THAT(status_or_int.value(), Eq(0)) << infile << " is not a PNG file";

  sapi::v::ConstCStr ver_string_var(PNG_LIBPNG_VER_STRING);
  sapi::v::NullPtr null = sapi::v::NullPtr();
  absl::StatusOr<png_structp> status_or_png_structp =
      api.png_create_read_struct_wrapper(ver_string_var.PtrBefore(), &null);

  ASSERT_THAT(status_or_png_structp, IsOk());
  sapi::v::RemotePtr struct_ptr(status_or_png_structp.value());
  ASSERT_THAT(struct_ptr.GetValue(), NotNull())
      << "png_create_read_struct_wrapper failed";

  absl::StatusOr<png_infop> status_or_png_infop =
      api.png_create_info_struct(&struct_ptr);

  ASSERT_THAT(status_or_png_infop, IsOk());
  sapi::v::RemotePtr info_ptr(status_or_png_infop.value());
  ASSERT_THAT(info_ptr.GetValue(), NotNull())
      << "png_create_info_struct failed";

  ASSERT_THAT(api.png_setjmp(&struct_ptr), IsOk());
  ASSERT_THAT(api.png_init_io_wrapper(&struct_ptr, &file), IsOk());
  ASSERT_THAT(api.png_set_sig_bytes(&struct_ptr, header.GetSize()), IsOk());
  ASSERT_THAT(api.png_read_info(&struct_ptr, &info_ptr), IsOk());

  status_or_int = api.png_get_image_width(&struct_ptr, &info_ptr);
  ASSERT_THAT(status_or_int, IsOk());
  data.width = status_or_int.value();
  EXPECT_THAT(data.width, Gt(0));

  status_or_int = api.png_get_image_height(&struct_ptr, &info_ptr);
  ASSERT_THAT(status_or_int, IsOk());
  data.height = status_or_int.value();
  EXPECT_THAT(data.height, Gt(0));

  absl::StatusOr<uint8_t> status_or_uchar =
      api.png_get_color_type(&struct_ptr, &info_ptr);
  ASSERT_THAT(status_or_uchar, IsOk());
  data.color_type = status_or_uchar.value();

  status_or_uchar = api.png_get_bit_depth(&struct_ptr, &info_ptr);
  ASSERT_THAT(status_or_uchar, IsOk());
  data.bit_depth = status_or_uchar.value();

  status_or_int = api.png_set_interlace_handling(&struct_ptr);
  ASSERT_THAT(status_or_int, IsOk());
  data.number_of_passes = status_or_int.value();

  ASSERT_THAT(api.png_read_update_info(&struct_ptr, &info_ptr), IsOk());
  ASSERT_THAT(api.png_setjmp(&struct_ptr), IsOk());

  absl::StatusOr<uint32_t> status_or_uint =
      api.png_get_rowbytes(&struct_ptr, &info_ptr);
  ASSERT_THAT(status_or_uint, IsOk());
  data.rowbytes = status_or_uint.value();
  EXPECT_THAT(data.rowbytes, Ge(data.width));

  data.row_pointers =
      std::make_unique<sapi::v::Array<uint8_t>>(data.height * data.rowbytes);

  ASSERT_THAT(
      api.png_read_image_wrapper(&struct_ptr, data.row_pointers->PtrAfter(),
                                 data.height, data.rowbytes),
      IsOk());

  ASSERT_THAT(api.png_fclose(&file), IsOk());
}

void WritePng(LibPNGApi& api, absl::string_view outfile, Data& data) {
  sapi::v::Fd fd(open(outfile.data(), O_WRONLY));

  ASSERT_THAT(fd.GetValue(), Ge(0)) << "Error opening output file";
  ASSERT_THAT((&api)->sandbox()->TransferToSandboxee(&fd), IsOk());

  ASSERT_THAT(fd.GetRemoteFd(), Ge(0)) << "Error receiving remote FD";

  sapi::v::ConstCStr wb_var("wb");
  absl::StatusOr<void*> status_or_file =
      api.png_fdopen(fd.GetRemoteFd(), wb_var.PtrBefore());
  ASSERT_THAT(status_or_file, IsOk());

  sapi::v::RemotePtr file(status_or_file.value());
  ASSERT_THAT(file.GetValue(), NotNull()) << "Could not open " << outfile;

  sapi::v::ConstCStr ver_string_var(PNG_LIBPNG_VER_STRING);
  sapi::v::NullPtr null = sapi::v::NullPtr();
  absl::StatusOr<png_structp> status_or_png_structp =
      api.png_create_write_struct_wrapper(ver_string_var.PtrBefore(), &null);
  ASSERT_THAT(status_or_png_structp, IsOk());

  sapi::v::RemotePtr struct_ptr(status_or_png_structp.value());
  ASSERT_THAT(struct_ptr.GetValue(), NotNull())
      << "png_create_write_struct_wrapper failed";

  absl::StatusOr<png_infop> status_or_png_infop =
      api.png_create_info_struct(&struct_ptr);
  ASSERT_THAT(status_or_png_infop, IsOk());

  sapi::v::RemotePtr info_ptr(status_or_png_infop.value());
  ASSERT_THAT(info_ptr.GetValue(), NotNull())
      << "png_create_info_struct failed";

  ASSERT_THAT(api.png_setjmp(&struct_ptr), IsOk());
  ASSERT_THAT(api.png_init_io_wrapper(&struct_ptr, &file), IsOk());

  ASSERT_THAT(api.png_setjmp(&struct_ptr), IsOk());
  ASSERT_THAT(
      api.png_set_IHDR(&struct_ptr, &info_ptr, data.width, data.height,
                       data.bit_depth, data.color_type, PNG_INTERLACE_NONE,
                       PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE),
      IsOk());

  ASSERT_THAT(api.png_write_info(&struct_ptr, &info_ptr), IsOk());

  ASSERT_THAT(api.png_setjmp(&struct_ptr), IsOk());
  ASSERT_THAT(
      api.png_write_image_wrapper(&struct_ptr, data.row_pointers->PtrBefore(),
                                  data.height, data.rowbytes),
      IsOk());

  ASSERT_THAT(api.png_setjmp(&struct_ptr), IsOk());
  ASSERT_THAT(api.png_write_end(&struct_ptr, &null), IsOk());

  ASSERT_THAT(api.png_fclose(&file), IsOk());
}

TEST(SandboxTest, ReadModifyWrite) {
  std::string infile = GetFilePath("red_ball.png");
  std::string outfile = GetFilePath("test_output.png");

  LibPNGSapiSandbox sandbox;
  ASSERT_THAT(sandbox.Init(), IsOk());
  LibPNGApi api(&sandbox);

  Data data;
  ReadPng(api, infile, data);

  ASSERT_THAT(data.color_type == PNG_COLOR_TYPE_RGBA ||
                  data.color_type == PNG_COLOR_TYPE_RGB,
              IsTrue())
      << infile << " has unexpected color type. Expected RGB or RGBA";

  size_t channel_count = 3;
  if (data.color_type == PNG_COLOR_TYPE_RGBA) {
    channel_count = 4;
  }

  EXPECT_THAT(channel_count * data.width, Eq(data.rowbytes));

  // RGB to BGR
  for (size_t i = 0; i != data.height; ++i) {
    for (size_t j = 0; j != data.width; ++j) {
      uint8_t r = (*data.row_pointers)[i * data.rowbytes + j * channel_count];
      uint8_t b =
          (*data.row_pointers)[i * data.rowbytes + j * channel_count + 2];
      (*data.row_pointers)[i * data.rowbytes + j * channel_count] = b;
      (*data.row_pointers)[i * data.rowbytes + j * channel_count + 2] = r;
    }
  }

  WritePng(api, outfile, data);

  Data result;
  ReadPng(api, outfile, result);

  EXPECT_THAT(result.height, Eq(data.height));
  EXPECT_THAT(result.width, Eq(data.width));
  EXPECT_THAT(result.color_type, Eq(data.color_type));
  EXPECT_THAT(result.rowbytes, Eq(data.rowbytes));
  EXPECT_THAT(result.bit_depth, Eq(data.bit_depth));
  EXPECT_THAT(result.number_of_passes, Eq(data.number_of_passes));
  EXPECT_THAT(absl::MakeSpan(result.row_pointers->GetData(),
                             result.row_pointers->GetSize()),
              ContainerEq(absl::MakeSpan(data.row_pointers->GetData(),
                                         data.row_pointers->GetSize())));
}

}  // namespace
