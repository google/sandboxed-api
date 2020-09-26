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

// sapi functions:
//  TIFFTileSize
//  TIFFOpen
//  TIFFReadEncodedTile
//  TIFFSetField
//  TIFFClose
//  TIFFReadRGBATile
//  TIFFGetField

constexpr std::array<uint8_t, 6> kCluster0 = {0, 0, 2, 0, 138, 139};
constexpr std::array<uint8_t, 6> kCluster64 = {0, 0, 9, 6, 134, 119};
constexpr std::array<uint8_t, 6> kCluster128 = {44, 40, 63, 59, 230, 95};

namespace {

absl::Status CheckCluster(int cluster, const sapi::v::Array<uint8_t>& buffer,
                          const std::array<uint8_t, 6>& expected_cluster) {
  uint8_t* target = buffer.GetData() + cluster * 6;

  if (!std::memcmp(target, expected_cluster.data(), 6)) {
    return absl::OkStatus();
  }

  return absl::InternalError(
      absl::StrCat("Cluster ", cluster, " did not match expected results.\n",
                   "Expect: ", expected_cluster[0], "\t", expected_cluster[1],
                   "\t", expected_cluster[4], "\t", expected_cluster[5], "\t",
                   expected_cluster[2], "\t", expected_cluster[3], "\n"));
}

absl::Status CheckRgbPixel(int pixel, int min_red, int max_red, int min_green,
                           int max_green, int min_blue, int max_blue,
                           const sapi::v::Array<uint8_t>& buffer) {
  uint8_t* rgb = buffer.GetData() + 3 * pixel;

  if (rgb[0] >= min_red && rgb[0] <= max_red && rgb[1] >= min_green &&
      rgb[1] <= max_green && rgb[2] >= min_blue && rgb[2] <= max_blue) {
    return absl::OkStatus();
  }

  return absl::InternalError(absl::StrCat(
      "Pixel ", pixel, " did not match expected results.\n", "Got R=", rgb[0],
      " (expected ", min_red, "..", max_red, "), G=", rgb[1], " (expected ",
      min_green, "..", max_green, "), B=", rgb[2], " (expected ", min_blue,
      "..", max_blue, ")\n"));
}

absl::Status CheckRgbaPixel(int pixel, int min_red, int max_red, int min_green,
                            int max_green, int min_blue, int max_blue,
                            int min_alpha, int max_alpha,
                            const sapi::v::Array<unsigned>& buffer) {
  // RGBA images are upside down - adjust for normal ordering
  int adjusted_pixel = pixel % 128 + (127 - (pixel / 128)) * 128;
  uint32 rgba = buffer[adjusted_pixel];

  if (TIFFGetR(rgba) >= (uint32)min_red && TIFFGetR(rgba) <= (uint32)max_red &&
      TIFFGetG(rgba) >= (uint32)min_green &&
      TIFFGetG(rgba) <= (uint32)max_green &&
      TIFFGetB(rgba) >= (uint32)min_blue &&
      TIFFGetB(rgba) <= (uint32)max_blue &&
      TIFFGetA(rgba) >= (uint32)min_alpha &&
      TIFFGetA(rgba) <= (uint32)max_alpha) {
    return absl::OkStatus();
  }

  return absl::InternalError(absl::StrCat(
      "Pixel ", pixel, " did not match expected results.\n",
      "Got R=", TIFFGetR(rgba), " (expected ", min_red, "..", max_red,
      "), G=", TIFFGetG(rgba), " (expected ", min_green, "..", max_green,
      "), B=", TIFFGetB(rgba), " (expected ", min_blue, "..", max_blue, "), A=",
      TIFFGetA(rgba), " (expected ", min_alpha, "..", max_alpha, ")\n"));
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
    LOG(ERROR) << "Something went wrong: CWD don't contain build dir. "
               << "Please run tests from build dir or send project dir as a "
               << "parameter: ./sandboxed /absolute/path/to/project/dir \n";
    project_path = cwd;
  } else {
    project_path = cwd.substr(0, find);
  }

  return sandbox2::file::JoinPath(project_path, "test", "images", filename);
}

absl::Status LibTIFFMain(const std::string& srcfile) {
  // without addDir to sandbox. to add dir use
  // sandbox(absolute_path_to_dir, srcfile) or
  // sandbox(absolute_path_to_dir). file and dir should be exists.
  // srcfile must also be absolute_path_to_file
  TiffSapiSandbox sandbox("", srcfile);

  // initialize sapi vars after constructing TiffSapiSandbox
  sapi::v::UShort h, v;
  sapi::StatusOr<TIFF*> status_or_tif;
  sapi::StatusOr<int> status_or_int;
  sapi::StatusOr<tmsize_t> status_or_long;
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
    // tif is NULL
    return absl::InternalError(absl::StrCat("Could not open ", srcfile));
  }

  SAPI_ASSIGN_OR_RETURN(status_or_int,
                        api.TIFFGetField2(&tif, TIFFTAG_YCBCRSUBSAMPLING,
                                          h.PtrBoth(), v.PtrBoth()));
  if (status_or_int.value() == 0 || h.GetValue() != 2 || v.GetValue() != 2) {
    return absl::InternalError("Could not retrieve subsampling tag");
  }

  SAPI_ASSIGN_OR_RETURN(status_or_long, api.TIFFTileSize(&tif));
  if (status_or_long.value() != 24576) {
    return absl::InternalError(
        absl::StrCat("tiles are ", status_or_long.value(), " bytes\n"));
  }
  tsize_t sz = status_or_long.value();

  sapi::v::Array<uint8_t> buffer_(sz);
  SAPI_ASSIGN_OR_RETURN(
      status_or_long, api.TIFFReadEncodedTile(&tif, 9, buffer_.PtrBoth(), sz));
  if (status_or_long.value() != sz) {
    return absl::InternalError(absl::StrCat(
        "Did not get expected result code from TIFFReadEncodedTile(): ",
        status_or_long.value(), " instead of ", sz));
  }

  unsigned pixel_status = 1;
  if (status = CheckCluster(0, buffer_, kCluster0); !status.ok()) {
    LOG(ERROR) << "CheckCluster failed:\n" << status.ToString();
  }
  pixel_status &= status.ok();

  if (status = CheckCluster(64, buffer_, kCluster64); !status.ok()) {
    LOG(ERROR) << "CheckCluster failed:\n" << status.ToString();
  }
  pixel_status &= status.ok();

  if (status = CheckCluster(128, buffer_, kCluster128); !status.ok()) {
    LOG(ERROR) << "CheckCluster failed:\n" << status.ToString();
  }
  pixel_status &= status.ok();

  if (!pixel_status) {
    return absl::InternalError("unexpected pixel_status value");
  }

  SAPI_ASSIGN_OR_RETURN(
      status_or_int,
      api.TIFFSetFieldU1(&tif, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB));
  if (!status_or_int.value()) {
    return absl::InternalError("TIFFSetFieldU1 not available");
  }

  SAPI_ASSIGN_OR_RETURN(status_or_long, api.TIFFTileSize(&tif));
  if (status_or_long.value() != 128 * 128 * 3) {
    return absl::InternalError(
        absl::StrCat("tiles are ", status_or_long.value(), " bytes"));
  }
  sz = status_or_long.value();

  sapi::v::Array<uint8_t> buffer2_(sz);

  SAPI_ASSIGN_OR_RETURN(
      status_or_long, api.TIFFReadEncodedTile(&tif, 9, buffer2_.PtrBoth(), sz));
  if (status_or_long.value() != sz) {
    return absl::InternalError(absl::StrCat(
        "Did not get expected result code from TIFFReadEncodedTile(): ",
        status_or_long.value(), " instead of ", sz));
  }

  pixel_status = 1;
  if (status = CheckRgbPixel(0, 15, 18, 0, 0, 18, 41, buffer2_); !status.ok()) {
    LOG(ERROR) << "CheckRgbPixel failed:\n" << status.ToString();
  }
  pixel_status &= status.ok();

  if (status = CheckRgbPixel(64, 0, 0, 0, 0, 0, 2, buffer2_); !status.ok()) {
    LOG(ERROR) << "CheckRgbPixel failed:\n" << status.ToString();
  }
  pixel_status &= status.ok();

  if (status = CheckRgbPixel(512, 5, 6, 34, 36, 182, 196, buffer2_);
      !status.ok()) {
    LOG(ERROR) << "CheckRgbPixel failed:\n" << status.ToString();
  }
  pixel_status &= status.ok();

  SAPI_RETURN_IF_ERROR(api.TIFFClose(&tif));

  SAPI_ASSIGN_OR_RETURN(
      status_or_tif, api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore()));

  sapi::v::RemotePtr tif2(status_or_tif.value());
  if (!tif2.GetValue()) {  // tif is NULL
    return absl::InternalError(absl::StrCat("Could not reopen ", srcfile));
  }

  sapi::v::Array<uint32> rgba_buffer_(128 * 128);

  SAPI_ASSIGN_OR_RETURN(
      status_or_int,
      api.TIFFReadRGBATile(&tif2, 1 * 128, 2 * 128, rgba_buffer_.PtrBoth()));
  if (!status_or_int.value()) {
    return absl::InternalError("TIFFReadRGBATile() returned failure code");
  }

  if (status = CheckRgbaPixel(0, 15, 18, 0, 0, 18, 41, 255, 255, rgba_buffer_);
      !status.ok()) {
    LOG(ERROR) << "CheckRgbaPixel failed:\n" << status.ToString();
  }
  pixel_status &= status.ok();

  if (status = CheckRgbaPixel(64, 0, 0, 0, 0, 0, 2, 255, 255, rgba_buffer_);
      !status.ok()) {
    LOG(ERROR) << "CheckRgbaPixel failed:\n" << status.ToString();
  }
  pixel_status &= status.ok();

  if (status =
          CheckRgbaPixel(512, 5, 6, 34, 36, 182, 196, 255, 255, rgba_buffer_);
      !status.ok()) {
    LOG(ERROR) << "CheckRgbaPixel failed:\n" << status.ToString();
  }
  pixel_status &= status.ok();

  SAPI_RETURN_IF_ERROR(api.TIFFClose(&tif2));

  if (!pixel_status) {
    return absl::InternalError("unexpected pixel_status value");
  }

  return absl::OkStatus();
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::string srcfile;
  // "test/images/quad-tile.jpg.tiff"
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
