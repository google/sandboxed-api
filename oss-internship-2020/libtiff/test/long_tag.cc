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

#include "check_tag.h"
#include "tiffio.h"  // NOLINT(build/include)

namespace {

struct LongTag {
  ttag_t tag;
  short count;
  unsigned value;
};

constexpr std::array<LongTag, 1> kLongTags = {
    {TIFFTAG_SUBFILETYPE, 1,
     FILETYPE_REDUCEDIMAGE | FILETYPE_PAGE | FILETYPE_MASK}};
constexpr unsigned kSamplePerPixel = 3;
constexpr unsigned kWidth = 1;
constexpr unsigned kLength = 1;
constexpr unsigned kBps = 8;
constexpr unsigned kRowsPerStrip = 1;

TEST(SandboxTest, LongTag) {
  sapi::StatusOr<std::string> status_or_path =
      sandbox2::CreateNamedTempFileAndClose("long_test.tif");
  ASSERT_THAT(status_or_path, IsOk()) << "Could not create temp file";

  std::string srcfile = sandbox2::file::JoinPath(
      sandbox2::file_util::fileops::GetCWD(), status_or_path.value());

  TiffSapiSandbox sandbox(srcfile);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";

  std::array<uint8_t, kSamplePerPixel> buffer = {0, 127, 255};
  sapi::v::Array<uint8_t> buffer_(buffer.data(), kSamplePerPixel);

  sapi::StatusOr<int> status_or_int;
  sapi::StatusOr<TIFF*> status_or_tif;

  TiffApi api(&sandbox);
  sapi::v::ConstCStr srcfile_var(srcfile.c_str());
  sapi::v::ConstCStr w_var("w");

  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), w_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "Could not open " << srcfile;

  sapi::v::RemotePtr tif(status_or_tif.value());
  ASSERT_THAT(tif.GetValue(), NotNull())
      << "Can't create test TIFF file " << srcfile;

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_IMAGEWIDTH, kWidth);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set ImageWidth tag";

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_IMAGELENGTH, kLength);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set ImageLength tag";

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_BITSPERSAMPLE, kBps);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set BitsPerSample tag";

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_SAMPLESPERPIXEL, kSamplePerPixel);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set SamplesPerPixel tag";

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_ROWSPERSTRIP, kRowsPerStrip);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set RowsPerStrip tag";

  status_or_int =
      api.TIFFSetFieldU1(&tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set PlanarConfiguration tag";

  status_or_int =
      api.TIFFSetFieldU1(&tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set PhotometricInterpretation tag";

  for (auto& tag : kLongTags) {
    status_or_int = api.TIFFSetFieldU1(&tif, tag.tag, tag.value);
    ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
    EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set tag " << tag.tag;
  }

  status_or_int = api.TIFFWriteScanline(&tif, buffer_.PtrBoth(), 0, 0);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFWriteScanline fatal error";
  ASSERT_THAT(status_or_int.value(), Ne(-1)) << "Can't write image data";

  ASSERT_THAT(api.TIFFClose(&tif), IsOk()) << "TIFFClose fatal error";

  sapi::v::ConstCStr r_var("r");
  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "Could not open " << srcfile;

  sapi::v::RemotePtr tif2(status_or_tif.value());
  ASSERT_THAT(tif2.GetValue(), NotNull())
      << "Can't create test TIFF file " << srcfile;

  CheckLongField(api, tif2, TIFFTAG_IMAGEWIDTH, kWidth);
  CheckLongField(api, tif2, TIFFTAG_IMAGELENGTH, kLength);
  CheckLongField(api, tif2, TIFFTAG_ROWSPERSTRIP, kRowsPerStrip);

  for (auto& tag : kLongTags) {
    CheckLongField(api, tif2, tag.tag, tag.value);
  }

  ASSERT_THAT(api.TIFFClose(&tif2), IsOk()) << "TIFFClose fatal error";
  unlink(srcfile.c_str());
}

}  // namespace
