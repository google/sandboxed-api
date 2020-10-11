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

#include <array>
#include <cstring>

#include "helper.h"  // NOLINT(build/include)
#include "tiffio.h"  // NOLINT(build/include)

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::NotNull;

namespace {

struct ChannelLimits {
  uint8_t min_red;
  uint8_t max_red;
  uint8_t min_green;
  uint8_t max_green;
  uint8_t min_blue;
  uint8_t max_blue;
  uint8_t min_alpha;
  uint8_t max_alpha;
};

constexpr uint32_t kRawTileNumber = 9;
constexpr uint32_t kClusterSize = 6;
constexpr uint32_t kChannelsInPixel = 3;
constexpr uint32_t kTestCount = 3;
constexpr uint32_t kImageSize = 128 * 128;
constexpr uint32_t kClusterImageSize = 64 * 64;
using ClusterData = std::array<uint8_t, kClusterSize>;

constexpr std::array<std::pair<uint32_t, ClusterData>, kTestCount> kClusters = {
    {{0, {0, 0, 2, 0, 138, 139}},
     {64, {0, 0, 9, 6, 134, 119}},
     {128, {44, 40, 63, 59, 230, 95}}}};

constexpr std::array<std::pair<uint32_t, ChannelLimits>, kTestCount> kLimits = {
    {{0, {15, 18, 0, 0, 18, 41, 255, 255}},
     {64, {0, 0, 0, 0, 0, 2, 255, 255}},
     {512, {5, 6, 34, 36, 182, 196, 255, 255}}}};

bool CheckCluster(uint32_t cluster, const sapi::v::Array<uint8_t>& buffer,
                  const ClusterData& expected_cluster) {
  bool is_overrun = (buffer.GetSize() <= cluster * kClusterSize);
  EXPECT_THAT(is_overrun, IsFalse()) << "Overrun";

  if (is_overrun) {
    return true;
  }

  auto* target = buffer.GetData() + cluster * kClusterSize;
  bool comp =
      !(std::memcmp(target, expected_cluster.data(), kClusterSize) == 0);

  // the image is split on 6-bit clusters because it has YCbCr color format
  EXPECT_THAT(comp, IsFalse())
      << "Cluster " << cluster << " did not match expected results.\n"
      << "Expect: " << expected_cluster[0] << "\t" << expected_cluster[1]
      << "\t" << expected_cluster[2] << "\t" << expected_cluster[3] << "\t"
      << expected_cluster[4] << "\t" << expected_cluster[5] << "\n"
      << "Got: " << target[0] << "\t" << target[1] << "\t" << target[2] << "\t"
      << target[3] << "\t" << target[4] << "\t" << target[5];

  return comp;
}

bool CheckRgbPixel(uint32_t pixel, const ChannelLimits& limits,
                   const sapi::v::Array<uint8_t>& buffer) {
  bool is_overrun = (buffer.GetSize() <= pixel * kChannelsInPixel);
  EXPECT_THAT(is_overrun, IsFalse()) << "Overrun";

  if (is_overrun) {
    return true;
  }

  auto* rgb = buffer.GetData() + pixel * kChannelsInPixel;
  bool comp = !(rgb[0] >= limits.min_red && rgb[0] <= limits.max_red &&
                rgb[1] >= limits.min_green && rgb[1] <= limits.max_green &&
                rgb[2] >= limits.min_blue && rgb[2] <= limits.max_blue);

  EXPECT_THAT(comp, IsFalse())
      << "Pixel " << pixel << " did not match expected results.\n"
      << "Got R=" << rgb[0] << " (expected " << limits.min_red
      << "..=" << limits.max_red << "), G=" << rgb[1] << " (expected "
      << limits.min_green << "..=" << limits.max_green << "), B=" << rgb[2]
      << " (expected " << limits.min_blue << "..=" << limits.max_blue << ")";
  return comp;
}

bool CheckRgbaPixel(uint32_t pixel, const ChannelLimits& limits,
                    const sapi::v::Array<uint32_t>& buffer) {
  // RGBA images are upside down - adjust for normal ordering
  uint32_t adjusted_pixel = pixel % 128 + (127 - (pixel / 128)) * 128;

  bool is_overrun = (buffer.GetSize() <= adjusted_pixel);
  EXPECT_THAT(is_overrun, IsFalse()) << "Overrun";

  if (is_overrun) {
    return true;
  }

  auto* rgba = buffer[adjusted_pixel];
  bool comp = !(TIFFGetR(rgba) >= static_cast<unsigned>(limits.min_red) &&
                TIFFGetR(rgba) <= static_cast<unsigned>(limits.max_red) &&
                TIFFGetG(rgba) >= static_cast<unsigned>(limits.min_green) &&
                TIFFGetG(rgba) <= static_cast<unsigned>(limits.max_green) &&
                TIFFGetB(rgba) >= static_cast<unsigned>(limits.min_blue) &&
                TIFFGetB(rgba) <= static_cast<unsigned>(limits.max_blue) &&
                TIFFGetA(rgba) >= static_cast<unsigned>(limits.min_alpha) &&
                TIFFGetA(rgba) <= static_cast<unsigned>(limits.max_alpha));

  EXPECT_THAT(comp, IsFalse())
      << "Pixel " << pixel << " did not match expected results.\n"
      << "Got R=" << TIFFGetR(rgba) << " (expected " << limits.min_red
      << "..=" << limits.max_red << "), G=" << TIFFGetG(rgba) << " (expected "
      << limits.min_green << "..=" << limits.max_green
      << "), B=" << TIFFGetB(rgba) << " (expected " << limits.min_blue
      << "..=" << limits.max_blue << "), A=" << TIFFGetA(rgba) << " (expected "
      << limits.min_alpha << "..=" << limits.max_alpha << ")";
  return comp;
}

TEST(SandboxTest, RawDecode) {
  tsize_t sz;
  bool pixel_status = false;
  bool cluster_status = false;
  std::string srcfile = GetFilePath("quad-tile.jpg.tiff");

  TiffSapiSandbox sandbox(srcfile);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";

  sapi::v::UShort h;
  sapi::v::UShort v;
  absl::StatusOr<TIFF*> status_or_tif;
  absl::StatusOr<int> status_or_int;
  absl::StatusOr<tmsize_t> status_or_long;

  TiffApi api(&sandbox);
  sapi::v::ConstCStr srcfile_var(srcfile.c_str());
  sapi::v::ConstCStr r_var("r");

  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "Could not open " << srcfile;

  sapi::v::RemotePtr tif(status_or_tif.value());
  ASSERT_THAT(tif.GetValue(), NotNull())
      << "Could not open " << srcfile << ", TIFFOpen return NULL";

  status_or_int = api.TIFFGetField2(&tif, TIFFTAG_YCBCRSUBSAMPLING, h.PtrBoth(),
                                    v.PtrBoth());
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFGetField2 fatal error";
  EXPECT_THAT(
      status_or_int.value() == 0 || h.GetValue() != 2 || v.GetValue() != 2,
      IsFalse())
      << "Could not retrieve subsampling tag";

  status_or_long = api.TIFFTileSize(&tif);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFTileSize fatal error";
  EXPECT_THAT(status_or_long.value(), Eq(kClusterImageSize * kClusterSize))
      << "Unexpected TileSize " << status_or_long.value() << ". Expected "
      << kClusterImageSize * kClusterSize << " bytes\n";
  sz = status_or_long.value();

  sapi::v::Array<uint8_t> buffer_(sz);
  // Read a tile in decompressed form, but still YCbCr subsampled
  status_or_long =
      api.TIFFReadEncodedTile(&tif, kRawTileNumber, buffer_.PtrBoth(), sz);
  ASSERT_THAT(status_or_long, IsOk()) << "TIFFReadEncodedTile fatal error";
  EXPECT_THAT(status_or_long.value(), Eq(sz))
      << "Did not get expected result code from TIFFReadEncodedTile()("
      << (int)status_or_long.value() << " instead of " << (int)sz << ")";

  for (const auto& [id, data] : kClusters) {
    cluster_status |= CheckCluster(id, buffer_, data);
  }
  ASSERT_FALSE(cluster_status) << "Clusters did not match expected results";

  status_or_int =
      api.TIFFSetFieldU1(&tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "The JPEGCOLORMODE tag cannot be changed";

  status_or_long = api.TIFFTileSize(&tif);
  ASSERT_THAT(status_or_long, IsOk()) << "TIFFTileSize fatal error";
  EXPECT_THAT(status_or_long.value(), Eq(kImageSize * kChannelsInPixel))
      << "Unexpected TileSize " << status_or_long.value() << ". Expected "
      << kImageSize * kChannelsInPixel << " bytes\n";
  sz = status_or_long.value();

  sapi::v::Array<uint8_t> buffer2_(sz);
  status_or_long =
      api.TIFFReadEncodedTile(&tif, kRawTileNumber, buffer2_.PtrBoth(), sz);
  ASSERT_THAT(status_or_long, IsOk()) << "TIFFReadEncodedTile fatal error";
  EXPECT_THAT(status_or_long.value(), Eq(sz))
      << "Did not get expected result code from TIFFReadEncodedTile()("
      << status_or_long.value() << " instead of " << sz;

  for (const auto& [id, data] : kLimits) {
    pixel_status |= CheckRgbPixel(id, data, buffer2_);
  }

  ASSERT_THAT(api.TIFFClose(&tif), IsOk()) << "TIFFClose fatal error";

  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "TIFFOpen fatal error";

  sapi::v::RemotePtr tif2(status_or_tif.value());
  ASSERT_THAT(tif2.GetValue(), NotNull())
      << "Could not open " << srcfile << ", TIFFOpen return NULL";

  sapi::v::Array<uint32_t> rgba_buffer_(kImageSize);

  status_or_int =
      api.TIFFReadRGBATile(&tif2, 1 * 128, 2 * 128, rgba_buffer_.PtrBoth());
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFReadRGBATile fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFReadRGBATile() returned failure code";

  for (const auto& [id, data] : kLimits) {
    pixel_status |= CheckRgbaPixel(id, data, rgba_buffer_);
  }

  EXPECT_THAT(api.TIFFClose(&tif2), IsOk()) << "TIFFClose fatal error";
  EXPECT_THAT(pixel_status, IsFalse()) << "wrong encoding";
}

}  // namespace
