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

#include <array>
#include <cstdint>
#include <cstring>

#include "helper.h"  // NOLINT(build/include)
#include "gtest/gtest.h"
#include "tiffio.h"  // NOLINT(build/include)

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::NotNull;

// sapi functions:
//    TIFFOpen
//    TIFFClose
//    TIFFGetField
//    TIFFSetField
//    TIFFTileSize
//    TIFFReadRGBATile
//    TIFFReadEncodedTile

namespace {

constexpr std::array<uint8_t, 6> kCluster0 = {0, 0, 2, 0, 138, 139};
constexpr std::array<uint8_t, 6> kCluster64 = {0, 0, 9, 6, 134, 119};
constexpr std::array<uint8_t, 6> kCluster128 = {44, 40, 63, 59, 230, 95};

template <typename ArrayT>
int CheckCluster(int cluster, const sapi::v::Array<uint8_t>& buffer,
                 const ArrayT& expected_cluster) {
  uint8_t* target = buffer.GetData() + cluster * 6;

  bool comp = !(std::memcmp(target, expected_cluster.data(), 6) == 0);

  EXPECT_THAT(comp, IsFalse())
      << "Cluster " << cluster << " did not match expected results.\n"
      << "Expect: " << expected_cluster[0] << "\t" << expected_cluster[1]
      << "\t" << expected_cluster[4] << "\t" << expected_cluster[5] << "\t"
      << expected_cluster[2] << "\t" << expected_cluster[3] << "\n"
      << "Got: " << target[0] << "\t" << target[1] << "\t" << target[4] << "\t"
      << target[5] << "\t" << target[2] << "\t" << target[3];

  return comp;
}

int CheckRgbPixel(int pixel, int min_red, int max_red, int min_green,
                  int max_green, int min_blue, int max_blue,
                  const sapi::v::Array<uint8_t>& buffer) {
  uint8_t* rgb = buffer.GetData() + 3 * pixel;

  bool comp =
      !(rgb[0] >= min_red && rgb[0] <= max_red && rgb[1] >= min_green &&
        rgb[1] <= max_green && rgb[2] >= min_blue && rgb[2] <= max_blue);

  EXPECT_THAT(comp, IsFalse())
      << "Pixel " << pixel << " did not match expected results.\n"
      << "Got R=" << rgb[0] << " (expected " << min_red << ".." << max_red
      << "), G=" << rgb[1] << " (expected " << min_green << ".." << max_green
      << "), B=" << rgb[2] << " (expected " << min_blue << ".." << max_blue
      << ")";
  return comp;
}

int CheckRgbaPixel(int pixel, int min_red, int max_red, int min_green,
                   int max_green, int min_blue, int max_blue, int min_alpha,
                   int max_alpha, const sapi::v::Array<uint32_t>& buffer) {
  // RGBA images are upside down - adjust for normal ordering
  int adjusted_pixel = pixel % 128 + (127 - (pixel / 128)) * 128;
  unsigned rgba = buffer[adjusted_pixel];

  bool comp = !(TIFFGetR(rgba) >= static_cast<uint32_t>(min_red) &&
                TIFFGetR(rgba) <= static_cast<uint32_t>(max_red) &&
                TIFFGetG(rgba) >= static_cast<uint32_t>(min_green) &&
                TIFFGetG(rgba) <= static_cast<uint32_t>(max_green) &&
                TIFFGetB(rgba) >= static_cast<uint32_t>(min_blue) &&
                TIFFGetB(rgba) <= static_cast<uint32_t>(max_blue) &&
                TIFFGetA(rgba) >= static_cast<uint32_t>(min_alpha) &&
                TIFFGetA(rgba) <= static_cast<uint32_t>(max_alpha));

  EXPECT_THAT(comp, IsFalse())
      << "Pixel " << pixel << " did not match expected results.\n"
      << "Got R=" << TIFFGetR(rgba) << " (expected " << min_red << ".."
      << max_red << "), G=" << TIFFGetG(rgba) << " (expected " << min_green
      << ".." << max_green << "), B=" << TIFFGetB(rgba) << " (expected "
      << min_blue << ".." << max_blue << "), A=" << TIFFGetA(rgba)
      << " (expected " << min_alpha << ".." << max_alpha << ")";
  return comp;
}

TEST(SandboxTest, RawDecode) {
  std::string srcfile = GetFilePath("test/images/quad-tile.jpg.tiff");

  TiffSapiSandbox sandbox("", srcfile);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";

  tsize_t sz;
  unsigned int pixel_status = 0;
  sapi::v::UShort h;
  sapi::v::UShort v;

  TiffApi api(&sandbox);
  sapi::v::ConstCStr srcfile_var(srcfile.c_str());
  sapi::v::ConstCStr r_var("r");

  absl::StatusOr<TIFF*> status_or_tif =
      api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "Could not open " << srcfile;

  sapi::v::RemotePtr tif(status_or_tif.value());
  ASSERT_THAT(tif.GetValue(), NotNull())
      << "Could not open " << srcfile << ", TIFFOpen return NULL";

  absl::StatusOr<int> status_or_int = api.TIFFGetField2(
      &tif, TIFFTAG_YCBCRSUBSAMPLING, h.PtrBoth(), v.PtrBoth());
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFGetField2 fatal error";
  EXPECT_THAT(
      status_or_int.value() == 0 || h.GetValue() != 2 || v.GetValue() != 2,
      IsFalse())
      << "Could not retrieve subsampling tag";

  absl::StatusOr<tmsize_t> status_or_long = api.TIFFTileSize(&tif);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFTileSize fatal error";
  EXPECT_THAT(status_or_long.value(), Eq(24576))
      << "tiles are " << status_or_long.value() << " bytes";
  sz = status_or_long.value();

  sapi::v::Array<uint8_t> buffer_(sz);
  status_or_long = api.TIFFReadEncodedTile(&tif, 9, buffer_.PtrBoth(), sz);
  ASSERT_THAT(status_or_long, IsOk()) << "TIFFReadEncodedTile fatal error";
  EXPECT_THAT(status_or_long.value(), Eq(sz))
      << "Did not get expected result code from TIFFReadEncodedTile()("
      << static_cast<int>(status_or_long.value()) << " instead of " << (int)sz
      << ")";

  ASSERT_FALSE(CheckCluster(0, buffer_, kCluster0) ||
               CheckCluster(64, buffer_, kCluster64) ||
               CheckCluster(128, buffer_, kCluster128))
      << "Clusters did not match expected results";

  status_or_int =
      api.TIFFSetFieldU1(&tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFSetFieldU1 not available";

  status_or_long = api.TIFFTileSize(&tif);
  ASSERT_THAT(status_or_long, IsOk()) << "TIFFTileSize fatal error";
  EXPECT_THAT(status_or_long.value(), Eq(128 * 128 * 3))
      << "tiles are " << status_or_long.value() << " bytes";
  sz = status_or_long.value();

  sapi::v::Array<uint8_t> buffer2_(sz);
  status_or_long = api.TIFFReadEncodedTile(&tif, 9, buffer2_.PtrBoth(), sz);
  ASSERT_THAT(status_or_long, IsOk()) << "TIFFReadEncodedTile fatal error";
  EXPECT_THAT(status_or_long.value(), Eq(sz))
      << "Did not get expected result code from TIFFReadEncodedTile()("
      << status_or_long.value() << " instead of " << sz;

  pixel_status |= CheckRgbPixel(0, 15, 18, 0, 0, 18, 41, buffer2_);
  pixel_status |= CheckRgbPixel(64, 0, 0, 0, 0, 0, 2, buffer2_);
  pixel_status |= CheckRgbPixel(512, 5, 6, 34, 36, 182, 196, buffer2_);

  ASSERT_THAT(api.TIFFClose(&tif), IsOk()) << "TIFFClose fatal error";

  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "TIFFOpen fatal error";

  sapi::v::RemotePtr tif2(status_or_tif.value());
  ASSERT_THAT(tif2.GetValue(), NotNull())
      << "Could not open " << srcfile << ", TIFFOpen return NULL";

  sapi::v::Array<uint32_t> rgba_buffer_(128 * 128);

  status_or_int =
      api.TIFFReadRGBATile(&tif2, 1 * 128, 2 * 128, rgba_buffer_.PtrBoth());
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFReadRGBATile fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFReadRGBATile() returned failure code";

  pixel_status |=
      CheckRgbaPixel(0, 15, 18, 0, 0, 18, 41, 255, 255, rgba_buffer_);
  pixel_status |= CheckRgbaPixel(64, 0, 0, 0, 0, 0, 2, 255, 255, rgba_buffer_);
  pixel_status |=
      CheckRgbaPixel(512, 5, 6, 34, 36, 182, 196, 255, 255, rgba_buffer_);

  EXPECT_THAT(api.TIFFClose(&tif2), IsOk()) << "TIFFClose fatal error";

  EXPECT_THAT(pixel_status, IsFalse()) << "wrong encoding";
}

}  // namespace
