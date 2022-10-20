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
#include <iostream>
#include <vector>

#include "../sandboxed.h"  // NOLINT(build/include)
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/vars.h"
#include "tiffio.h"  // NOLINT(build/include)

// sapi functions:
//    TIFFTileSize
//    TIFFOpen
//    TIFFReadEncodedTile
//    TIFFSetField
//    TIFFClose
//    TIFFReadRGBATile
//    TIFFGetField

namespace {

constexpr std::array<uint8_t, 6> kCluster0 = {0, 0, 2, 0, 138, 139};
constexpr std::array<uint8_t, 6> kCluster64 = {0, 0, 9, 6, 134, 119};
constexpr std::array<uint8_t, 6> kCluster128 = {44, 40, 63, 59, 230, 95};

template <typename ArrayT>
int CheckCluster(int cluster, const sapi::v::Array<uint8_t>& buffer,
                 const ArrayT& expected_cluster) {
  uint8_t* target = buffer.GetData() + cluster * 6;

  if (!std::memcmp(target, expected_cluster.data(), 6)) {
    return 0;
  }

  std::cerr << "Cluster " << cluster << " did not match expected results.\n"
            << "Expect: " << expected_cluster[0] << "\t" << expected_cluster[1]
            << "\t" << expected_cluster[4] << "\t" << expected_cluster[5]
            << "\t" << expected_cluster[2] << "\t" << expected_cluster[3]
            << "\n"
            << "Got: " << target[0] << "\t" << target[1] << "\t" << target[4]
            << "\t" << target[5] << "\t" << target[2] << "\t" << target[3]
            << '\n';

  return 1;
}

int CheckRgbPixel(int pixel, int min_red, int max_red, int min_green,
                  int max_green, int min_blue, int max_blue,
                  const sapi::v::Array<uint8_t>& buffer) {
  uint8_t* rgb = buffer.GetData() + 3 * pixel;

  if (rgb[0] >= min_red && rgb[0] <= max_red && rgb[1] >= min_green &&
      rgb[1] <= max_green && rgb[2] >= min_blue && rgb[2] <= max_blue) {
    return 0;
  }

  std::cerr << "Pixel " << pixel << " did not match expected results.\n"
            << "Got R=" << rgb[0] << " (expected " << min_red << ".." << max_red
            << "), G=" << rgb[1] << " (expected " << min_green << ".."
            << max_green << "), B=" << rgb[2] << " (expected " << min_blue
            << ".." << max_blue << ")\n";
  return 1;
}

int CheckRgbaPixel(int pixel, int min_red, int max_red, int min_green,
                   int max_green, int min_blue, int max_blue, int min_alpha,
                   int max_alpha, const sapi::v::Array<uint32_t>& buffer) {
  // RGBA images are upside down - adjust for normal ordering
  int adjusted_pixel = pixel % 128 + (127 - (pixel / 128)) * 128;
  uint32_t rgba = buffer[adjusted_pixel];

  if (TIFFGetR(rgba) >= static_cast<uint32_t>(min_red) &&
      TIFFGetR(rgba) <= static_cast<uint32_t>(max_red) &&
      TIFFGetG(rgba) >= static_cast<uint32_t>(min_green) &&
      TIFFGetG(rgba) <= static_cast<uint32_t>(max_green) &&
      TIFFGetB(rgba) >= static_cast<uint32_t>(min_blue) &&
      TIFFGetB(rgba) <= static_cast<uint32_t>(max_blue) &&
      TIFFGetA(rgba) >= static_cast<uint32_t>(min_alpha) &&
      TIFFGetA(rgba) <= static_cast<uint32_t>(max_alpha)) {
    return 0;
  }

  std::cerr << "Pixel " << pixel << " did not match expected results.\n"
            << "Got R=" << TIFFGetR(rgba) << " (expected " << min_red << ".."
            << max_red << "), G=" << TIFFGetG(rgba) << " (expected "
            << min_green << ".." << max_green << "), B=" << TIFFGetB(rgba)
            << " (expected " << min_blue << ".." << max_blue
            << "), A=" << TIFFGetA(rgba) << " (expected " << min_alpha << ".."
            << max_alpha << ")\n";
  return 1;
}
std::string GetFilePath(const std::string& dir, const std::string& filename) {
  return sapi::file::JoinPath(dir, "test", "images", filename);
}

std::string GetCWD() {
  char cwd[PATH_MAX];
  getcwd(cwd, sizeof(cwd));
  return cwd;
}

std::string GetFilePath(const std::string filename) {
  std::string cwd = GetCWD();
  auto find = cwd.rfind("build");

  std::string project_path;
  if (find == std::string::npos) {
    std::cerr << "Something went wrong: CWD don't contain build dir. "
              << "Please run tests from build dir or send project dir as a "
              << "parameter: ./sandboxed /absolute/path/to/project/dir \n";
    project_path = cwd;
  } else {
    project_path = cwd.substr(0, find);
  }

  return sapi::file::JoinPath(project_path, "test", "images", filename);
}

}  // namespace

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  std::string srcfile;
  // "test/images/quad-tile.jpg.tiff"
  std::string srcfilerel = "quad-tile.jpg.tiff";

  if (argc < 2) {
    srcfile = GetFilePath(srcfilerel);
  } else {
    srcfile = GetFilePath(argv[1], srcfilerel);
  }

  // without addDir to sandbox. to add dir use
  // sandbox(absolute_path_to_dir, srcfile) or
  // sandbox(absolute_path_to_dir). file and dir should be exists.
  // srcfile must also be absolute_path_to_file
  TiffSapiSandbox sandbox("", srcfile);

  // initialize sapi vars after constructing TiffSapiSandbox
  sapi::v::UShort h, v;
  absl::StatusOr<TIFF*> status_or_tif;
  absl::StatusOr<int> status_or_int;
  absl::StatusOr<tmsize_t> status_or_long;

  auto status = sandbox.Init();
  if (!status.ok()) {
    fprintf(stderr, "Couldn't initialize Sandboxed API: %s\n", status);
  }

  TiffApi api(&sandbox);
  sapi::v::ConstCStr srcfile_var(srcfile.c_str());
  sapi::v::ConstCStr r_var("r");

  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore());
  if (!status_or_tif.ok()) {
    std::cerr << "Could not open " << srcfile << ", TIFFError\n";
    return 1;
  }

  sapi::v::RemotePtr tif(status_or_tif.value());
  if (!tif.GetValue()) {
    // tif is NULL
    std::cerr << "Could not open " << srcfile << "\n";
    return 1;
  }

  status_or_int = api.TIFFGetField2(&tif, TIFFTAG_YCBCRSUBSAMPLING, h.PtrBoth(),
                                    v.PtrBoth());
  if (!status_or_int.ok() || status_or_int.value() == 0 || h.GetValue() != 2 ||
      v.GetValue() != 2) {
    std::cerr << "Could not retrieve subsampling tag\n";
    return 1;
  }

  status_or_long = api.TIFFTileSize(&tif);
  if (!status_or_int.ok() || status_or_long.value() != 24576) {
    std::cerr << "tiles are " << status_or_long.value() << " bytes\n";
    exit(1);
  }
  tsize_t sz = status_or_long.value();

  sapi::v::Array<uint8_t> buffer_(sz);
  status_or_long = api.TIFFReadEncodedTile(&tif, 9, buffer_.PtrBoth(), sz);
  if (!status_or_long.ok() || status_or_long.value() != sz) {
    std::cerr << "Did not get expected result code from"
              << "TIFFReadEncodedTile(): (" << status_or_long.value()
              << " instead of " << sz << ")\n";
    return 1;
  }

  if (CheckCluster(0, buffer_, kCluster0) ||
      CheckCluster(64, buffer_, kCluster64) ||
      CheckCluster(128, buffer_, kCluster128)) {
    return 1;
  }

  status_or_int =
      api.TIFFSetFieldU1(&tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
  if (!status_or_int.ok() || !status_or_int.value()) {
    std::cerr << "TIFFSetFieldU1 not available\n";
  }

  status_or_long = api.TIFFTileSize(&tif);
  if (!status_or_long.ok() || status_or_long.value() != 128 * 128 * 3) {
    std::cerr << "tiles are " << status_or_long.value() << " bytes\n";
    return 1;
  }
  sz = status_or_long.value();

  sapi::v::Array<uint8_t> buffer2_(sz);

  status_or_long = api.TIFFReadEncodedTile(&tif, 9, buffer2_.PtrBoth(), sz);
  if (!status_or_long.ok() || status_or_long.value() != sz) {
    std::cerr << "Did not get expected result code from "
              << "TIFFReadEncodedTile(): (" << status_or_long.value()
              << " instead of " << sz << ")\n";
    return 1;
  }

  uint32_t pixel_status = 0;
  pixel_status |= CheckRgbPixel(0, 15, 18, 0, 0, 18, 41, buffer2_);
  pixel_status |= CheckRgbPixel(64, 0, 0, 0, 0, 0, 2, buffer2_);
  pixel_status |= CheckRgbPixel(512, 5, 6, 34, 36, 182, 196, buffer2_);

  if (!api.TIFFClose(&tif).ok()) {
    std::cerr << "TIFFClose error\n";
  }

  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore());
  if (!status_or_tif.ok()) {
    std::cerr << "Could not reopen " << srcfile << "\n";
    return 1;
  }

  sapi::v::RemotePtr tif2(status_or_tif.value());
  if (!tif2.GetValue()) {  // tif is NULL
    std::cerr << "Could not reopen " << srcfile << "\n";
    return 1;
  }

  sapi::v::Array<uint32> rgba_buffer_(128 * 128);

  status_or_int =
      api.TIFFReadRGBATile(&tif2, 1 * 128, 2 * 128, rgba_buffer_.PtrBoth());
  if (!status_or_int.ok() || !status_or_int.value()) {
    fprintf(stderr, "TIFFReadRGBATile() returned failure code.\n");
    return 1;
  }

  pixel_status |=
      CheckRgbaPixel(0, 15, 18, 0, 0, 18, 41, 255, 255, rgba_buffer_);
  pixel_status |= CheckRgbaPixel(64, 0, 0, 0, 0, 0, 2, 255, 255, rgba_buffer_);
  pixel_status |=
      CheckRgbaPixel(512, 5, 6, 34, 36, 182, 196, 255, 255, rgba_buffer_);

  if (!api.TIFFClose(&tif2).ok()) {
    std::cerr << "TIFFClose erro\n";
  }

  if (pixel_status) {
    std::cerr << "pixel_status is true, expected false";
    return 1;
  }

  return 0;
}
