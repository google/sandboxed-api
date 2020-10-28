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

#include <cstring>

#include "../sandboxed.h"  // NOLINT(build/include)
#include "../test/data.h"  // NOLINT(build/include)
#include "absl/algorithm/container.h"
#include "absl/strings/str_join.h"
#include "gtest/gtest.h"
#include "helper.h"  // NOLINT(build/include)
#include "sandboxed_api/util/status_matchers.h"
#include "tiffio.h"  // NOLINT(build/include)

namespace {

using ::sapi::IsOk;
using ::testing::ContainerEq;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::NotNull;

void CheckCluster(uint32_t cluster, const sapi::v::Array<uint8_t>& buffer,
                  const ClusterData& expected_cluster) {
  ASSERT_THAT(buffer.GetSize(), Ge((cluster + 1) * kClusterSize)) << "Overrun";

  // the image is split on 6-bit clusters because it has YCbCr color format
  EXPECT_THAT(
      absl::MakeSpan(buffer.GetData() + cluster * kClusterSize, kClusterSize),
      ContainerEq(absl::MakeSpan(expected_cluster)))
      << "Cluster " << cluster << " did not match expected results.";
}

void CheckRgbPixel(uint32_t pixel, const ChannelLimits& limits,
                   const sapi::v::Array<uint8_t>& buffer) {
  ASSERT_THAT(buffer.GetSize(), Ge((pixel + 1) * kChannelsInPixel))
      << "Overrun";

  uint8_t* rgb = buffer.GetData() + pixel * kChannelsInPixel;
  EXPECT_THAT(rgb[0], Ge(limits.min_red));
  EXPECT_THAT(rgb[0], Le(limits.max_red));
  EXPECT_THAT(rgb[1], Ge(limits.min_green));
  EXPECT_THAT(rgb[1], Le(limits.max_green));
  EXPECT_THAT(rgb[2], Ge(limits.min_blue));
  EXPECT_THAT(rgb[2], Le(limits.max_blue));
}

void CheckRgbaPixel(uint32_t pixel, const ChannelLimits& limits,
                    const sapi::v::Array<uint32_t>& buffer) {
  // RGBA images are upside down - adjust for normal ordering
  uint32_t adjusted_pixel = pixel % 128 + (127 - (pixel / 128)) * 128;

  ASSERT_THAT(buffer.GetSize(), Gt(adjusted_pixel)) << "Overrun";

  uint32_t rgba = buffer[adjusted_pixel];
  EXPECT_THAT(TIFFGetR(rgba), Ge(limits.min_red));
  EXPECT_THAT(TIFFGetR(rgba), Le(limits.max_red));
  EXPECT_THAT(TIFFGetG(rgba), Ge(limits.min_green));
  EXPECT_THAT(TIFFGetG(rgba), Le(limits.max_green));
  EXPECT_THAT(TIFFGetB(rgba), Ge(limits.min_blue));
  EXPECT_THAT(TIFFGetB(rgba), Le(limits.max_blue));
  EXPECT_THAT(TIFFGetA(rgba), Ge(limits.min_alpha));
  EXPECT_THAT(TIFFGetA(rgba), Le(limits.max_alpha));
}

TEST(SandboxTest, RawDecode) {
  std::string srcfile = GetFilePath("quad-tile.jpg.tiff");

  TiffSapiSandbox sandbox(srcfile);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";

  TiffApi api(&sandbox);
  sapi::v::ConstCStr srcfile_var(srcfile.c_str());
  sapi::v::ConstCStr r_var("r");

  absl::StatusOr<TIFF*> status_or_tif =
      api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "Could not open " << srcfile;

  sapi::v::RemotePtr tif(status_or_tif.value());
  ASSERT_THAT(tif.GetValue(), NotNull())
      << "Could not open " << srcfile << ", TIFFOpen return NULL";

  sapi::v::UShort h;
  sapi::v::UShort v;
  absl::StatusOr<int> status_or_int = api.TIFFGetField2(
      &tif, TIFFTAG_YCBCRSUBSAMPLING, h.PtrAfter(), v.PtrAfter());
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFGetField2 fatal error";
  EXPECT_THAT(
      status_or_int.value() == 0 || h.GetValue() != 2 || v.GetValue() != 2,
      IsFalse())
      << "Could not retrieve subsampling tag";

  tsize_t sz;
  absl::StatusOr<tmsize_t> status_or_long = api.TIFFTileSize(&tif);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFTileSize fatal error";
  EXPECT_THAT(status_or_long.value(), Eq(kClusterImageSize * kClusterSize))
      << "Unexpected TileSize " << status_or_long.value() << ". Expected "
      << kClusterImageSize * kClusterSize << " bytes";
  sz = status_or_long.value();

  sapi::v::Array<uint8_t> buffer(sz);
  // Read a tile in decompressed form, but still YCbCr subsampled
  status_or_long =
      api.TIFFReadEncodedTile(&tif, kRawTileNumber, buffer.PtrAfter(), sz);
  ASSERT_THAT(status_or_long, IsOk()) << "TIFFReadEncodedTile fatal error";
  EXPECT_THAT(status_or_long.value(), Eq(sz))
      << "Did not get expected result code from TIFFReadEncodedTile()("
      << static_cast<int>(status_or_long.value()) << " instead of "
      << static_cast<int>(sz) << ")";

  for (const auto& [id, data] : kClusters) {
    CheckCluster(id, buffer, data);
  }

  status_or_int =
      api.TIFFSetFieldU1(&tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "The JPEGCOLORMODE tag cannot be changed";

  status_or_long = api.TIFFTileSize(&tif);
  ASSERT_THAT(status_or_long, IsOk()) << "TIFFTileSize fatal error";
  EXPECT_THAT(status_or_long.value(), Eq(kImageSize * kChannelsInPixel))
      << "Unexpected TileSize " << status_or_long.value() << ". Expected "
      << kImageSize * kChannelsInPixel << " bytes";
  sz = status_or_long.value();

  sapi::v::Array<uint8_t> buffer2(sz);
  status_or_long =
      api.TIFFReadEncodedTile(&tif, kRawTileNumber, buffer2.PtrAfter(), sz);
  ASSERT_THAT(status_or_long, IsOk()) << "TIFFReadEncodedTile fatal error";
  EXPECT_THAT(status_or_long.value(), Eq(sz))
      << "Did not get expected result code from TIFFReadEncodedTile()("
      << status_or_long.value() << " instead of " << sz;

  for (const auto& [id, data] : kLimits) {
    CheckRgbPixel(id, data, buffer2);
  }

  ASSERT_THAT(api.TIFFClose(&tif), IsOk()) << "TIFFClose fatal error";

  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "TIFFOpen fatal error";

  sapi::v::RemotePtr tif2(status_or_tif.value());
  ASSERT_THAT(tif2.GetValue(), NotNull())
      << "Could not open " << srcfile << ", TIFFOpen return NULL";

  sapi::v::Array<uint32_t> rgba_buffer(kImageSize);
  status_or_int =
      api.TIFFReadRGBATile(&tif2, 1 * 128, 2 * 128, rgba_buffer.PtrAfter());
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFReadRGBATile fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFReadRGBATile() returned failure code";

  for (const auto& [id, data] : kLimits) {
    CheckRgbaPixel(id, data, rgba_buffer);
  }

  EXPECT_THAT(api.TIFFClose(&tif2), IsOk()) << "TIFFClose fatal error";
}

}  // namespace
