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
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "../sandboxed.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/vars.h"
#include "tiffio.h"  // NOLINT(build/include)

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

constexpr unsigned kRawTileNumber = 9;
constexpr unsigned kClusterSize = 6;
constexpr unsigned kChannelsInPixel = 3;
constexpr unsigned kTestCount = 3;
constexpr unsigned kImageSize = 128 * 128;
constexpr unsigned kClusterImageSize = 64 * 64;
using ClusterData = std::array<uint8_t, kClusterSize>;

constexpr std::array<std::pair<unsigned, ClusterData>, kTestCount> kClusters = {
    {{0, {0, 0, 2, 0, 138, 139}},
     {64, {0, 0, 9, 6, 134, 119}},
     {128, {44, 40, 63, 59, 230, 95}}}};

constexpr std::array<std::pair<unsigned, ChannelLimits>, kTestCount> kLimits = {
    {{0, {15, 18, 0, 0, 18, 41, 255, 255}},
     {64, {0, 0, 0, 0, 0, 2, 255, 255}},
     {512, {5, 6, 34, 36, 182, 196, 255, 255}}}};

absl::Status CheckCluster(unsigned cluster,
                          const sapi::v::Array<uint8_t>& buffer,
                          const ClusterData& expected_cluster) {
  if (buffer.GetSize() <= cluster * kClusterSize) {
    return absl::InternalError("Buffer overrun\n");
  }
  auto target = buffer.GetData() + cluster * kClusterSize;

  if (!std::memcmp(target, expected_cluster.data(), kClusterSize)) {
    return absl::OkStatus();
  }

  // the image is split on 6-bit clusters because it has YCbCr color format
  return absl::InternalError(absl::StrCat(
      "Cluster ", cluster, " did not match expected results.\n", "Expect: ",
      expected_cluster[0], "\t", expected_cluster[1], "\t", expected_cluster[2],
      "\t", expected_cluster[3], "\t", expected_cluster[4], "\t",
      expected_cluster[5], "\n", "Got: ", target[0], "\t", target[1], "\t",
      target[2], "\t", target[3], "\t", target[4], "\t", target[5], "\n"));
}

absl::Status CheckRgbPixel(unsigned pixel, const ChannelLimits& limits,
                           const sapi::v::Array<uint8_t>& buffer) {
  if (buffer.GetSize() <= pixel * kChannelsInPixel) {
    return absl::InternalError("Buffer overrun\n");
  }
  auto rgb = buffer.GetData() + kChannelsInPixel * pixel;

  if (rgb[0] >= limits.min_red && rgb[0] <= limits.max_red &&
      rgb[1] >= limits.min_green && rgb[1] <= limits.max_green &&
      rgb[2] >= limits.min_blue && rgb[2] <= limits.max_blue) {
    return absl::OkStatus();
  }

  return absl::InternalError(absl::StrCat(
      "Pixel ", pixel, " did not match expected results.\n", "Got R=", rgb[0],
      " (expected ", limits.min_red, "..=", limits.max_red, "), G=", rgb[1],
      " (expected ", limits.min_green, "..=", limits.max_green, "), B=", rgb[2],
      " (expected ", limits.min_blue, "..=", limits.max_blue, ")\n"));
}

absl::Status CheckRgbaPixel(unsigned pixel, const ChannelLimits& limits,
                            const sapi::v::Array<unsigned>& buffer) {
  // RGBA images are upside down - adjust for normal ordering
  unsigned adjusted_pixel = pixel % 128 + (127 - (pixel / 128)) * 128;

  if (buffer.GetSize() <= adjusted_pixel) {
    return absl::InternalError("Buffer overrun\n");
  }

  auto rgba = buffer[adjusted_pixel];
  if (TIFFGetR(rgba) >= static_cast<unsigned>(limits.min_red) &&
      TIFFGetR(rgba) <= static_cast<unsigned>(limits.max_red) &&
      TIFFGetG(rgba) >= static_cast<unsigned>(limits.min_green) &&
      TIFFGetG(rgba) <= static_cast<unsigned>(limits.max_green) &&
      TIFFGetB(rgba) >= static_cast<unsigned>(limits.min_blue) &&
      TIFFGetB(rgba) <= static_cast<unsigned>(limits.max_blue) &&
      TIFFGetA(rgba) >= static_cast<unsigned>(limits.min_alpha) &&
      TIFFGetA(rgba) <= static_cast<unsigned>(limits.max_alpha)) {
    return absl::OkStatus();
  }

  return absl::InternalError(absl::StrCat(
      "Pixel ", pixel, " did not match expected results.\n", "Got R=",
      TIFFGetR(rgba), " (expected ", limits.min_red, "..=", limits.max_red,
      "), G=", TIFFGetG(rgba), " (expected ", limits.min_green,
      "..=", limits.max_green, "), B=", TIFFGetB(rgba), " (expected ",
      limits.min_blue, "..=", limits.max_blue, "), A=", TIFFGetA(rgba),
      " (expected ", limits.min_alpha, "..=", limits.max_alpha, ")\n"));
}

}  // namespace

std::string GetFilePath(const std::string& dir, const std::string& filename) {
  return sandbox2::file::JoinPath(dir, "test", "images", filename);
}

std::string GetFilePath(const std::string filename) {
  std::string cwd = sandbox2::file_util::fileops::GetCWD();
  auto find = cwd.rfind("build");

  std::string project_path;
  if (find == std::string::npos) {
    LOG(ERROR)
        << "Something went wrong: CWD don't contain build dir. "
        << "Please run tests from build dir or send project dir as a "
        << "parameter: ./sandboxed /absolute/path/to/project/dir .\n"
        << "Falling back to using current working directory as root dir.\n";
    project_path = cwd;
  } else {
    project_path = cwd.substr(0, find);
  }

  return sandbox2::file::JoinPath(project_path, "test", "images", filename);
}

absl::Status LibTIFFMain(const std::string& srcfile) {
  // to use dir and file inside sapi-libtiff, use
  // sandbox(file) – file only -- or
  // sandbox(file, dir) -- file and dir -- or
  // sandbox(nullopt, dir) -- dir only.
  // file and directory must exist.
  // all paths must be absolute.

  TiffSapiSandbox sandbox(srcfile);

  bool pixel_status = true;
  bool cluster_status = true;
  // initialize sapi vars after constructing TiffSapiSandbox
  sapi::v::UShort h, v;
  absl::StatusOr<TIFF*> status_or_tif;
  absl::StatusOr<int> status_or_int;
  absl::StatusOr<tmsize_t> status_or_long;
  absl::Status status;

  status = sandbox.Init();

  SAPI_RETURN_IF_ERROR(sandbox.Init());

  TiffApi api(&sandbox);
  sapi::v::ConstCStr srcfile_var(srcfile.c_str());
  sapi::v::ConstCStr r_var("r");

  SAPI_ASSIGN_OR_RETURN(
      status_or_tif, api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore()));

  sapi::v::RemotePtr tif(status_or_tif.value());
  if (!tif.GetValue()) {
    return absl::InternalError(absl::StrCat("Could not open ", srcfile));
  }

  SAPI_ASSIGN_OR_RETURN(auto return_value,
                        api.TIFFGetField2(&tif, TIFFTAG_YCBCRSUBSAMPLING,
                                          h.PtrBoth(), v.PtrBoth()));
  if (return_value == 0 || h.GetValue() != 2 || v.GetValue() != 2) {
    return absl::InternalError("Could not retrieve subsampling tag");
  }

  SAPI_ASSIGN_OR_RETURN(tsize_t sz, api.TIFFTileSize(&tif));
  if (sz != kClusterSize * kClusterImageSize) {
    return absl::InternalError(
        absl::StrCat("Unexpected TileSize ", sz, ". Expected ",
                     kClusterSize * kClusterImageSize, " bytes\n"));
  }

  sapi::v::Array<uint8_t> buffer_(sz);
  // Read a tile in decompressed form, but still YCbCr subsampled
  SAPI_ASSIGN_OR_RETURN(
      tsize_t new_sz,
      api.TIFFReadEncodedTile(&tif, kRawTileNumber, buffer_.PtrBoth(), sz));
  if (new_sz != sz) {
    return absl::InternalError(absl::StrCat(
        "Did not get expected result code from TIFFReadEncodedTile(): ",
        status_or_long.value(), " instead of ", sz));
  }

  for (const auto& [id, data] : kClusters) {
    if (status = CheckCluster(id, buffer_, data); !status.ok()) {
      LOG(ERROR) << "CheckCluster failed:\n" << status.ToString();
    }
    cluster_status &= status.ok();
  }

  if (!cluster_status) {
    return absl::InternalError("One or more clusters failed the check");
  }

  SAPI_ASSIGN_OR_RETURN(
      return_value,
      api.TIFFSetFieldU1(&tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB));
  if (return_value == 0) {
    return absl::InternalError("The JPEGCOLORMODE tag cannot be changed");
  }

  SAPI_ASSIGN_OR_RETURN(sz, api.TIFFTileSize(&tif));
  if (sz != kChannelsInPixel * kImageSize) {
    return absl::InternalError(
        absl::StrCat("Unexpected TileSize ", sz, ". Expected ",
                     kChannelsInPixel * kImageSize, " bytes\n"));
  }

  sapi::v::Array<uint8_t> buffer2_(sz);

  SAPI_ASSIGN_OR_RETURN(
      new_sz,
      api.TIFFReadEncodedTile(&tif, kRawTileNumber, buffer2_.PtrBoth(), sz));
  if (new_sz != sz) {
    return absl::InternalError(absl::StrCat(
        "Did not get expected result code from TIFFReadEncodedTile(): ", new_sz,
        " instead of ", sz));
  }

  for (const auto& [id, data] : kLimits) {
    if (status = CheckRgbPixel(id, data, buffer2_); !status.ok()) {
      LOG(ERROR) << "CheckRgbPixel failed:\n" << status.ToString();
    }
    pixel_status &= status.ok();
  }

  SAPI_RETURN_IF_ERROR(api.TIFFClose(&tif));

  SAPI_ASSIGN_OR_RETURN(
      status_or_tif, api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore()));

  sapi::v::RemotePtr tif2(status_or_tif.value());
  if (!tif2.GetValue()) {
    return absl::InternalError(absl::StrCat("Could not reopen ", srcfile));
  }

  sapi::v::Array<unsigned> rgba_buffer_(kImageSize);

  // read as rgba
  SAPI_ASSIGN_OR_RETURN(
      return_value,
      api.TIFFReadRGBATile(&tif2, 1 * 128, 2 * 128, rgba_buffer_.PtrBoth()));
  if (return_value == 0) {
    return absl::InternalError("TIFFReadRGBATile() returned failure code");
  }

  // Checking specific pixels from the test data, 0th, 64th and 512th
  for (const auto& [id, data] : kLimits) {
    if (status = CheckRgbaPixel(id, data, rgba_buffer_); !status.ok()) {
      LOG(ERROR) << "CheckRgbaPixel failed:\n" << status.ToString();
    }
    pixel_status &= status.ok();
  }

  SAPI_RETURN_IF_ERROR(api.TIFFClose(&tif2));

  if (!pixel_status) {
    return absl::InternalError("wrong encoding");
  }

  return absl::OkStatus();
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string srcfile;
  std::string srcfilerel = "quad-tile.jpg.tiff";

  if (argc < 2) {
    srcfile = GetFilePath(srcfilerel);
  } else {
    srcfile = GetFilePath(argv[1], srcfilerel);
  }

  auto status = LibTIFFMain(srcfile);
  if (!status.ok()) {
    LOG(ERROR) << "LibTIFFMain failed with error:\n"
               << status.ToString() << '\n';
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
