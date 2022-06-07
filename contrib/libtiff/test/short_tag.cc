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
#include <vector>

#include "check_tag.h"  // NOLINT(build/include)
#include "tiffio.h"     // NOLINT(build/include)

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::NotNull;

// sapi functions:
// TIFFWriteScanline
// TIFFOpen
// TIFFClose
// TIFFGetField (from check_tag.c)
// TIFFSetField

namespace {

constexpr int kSamplePerPixel = 3;
constexpr int kWidth = 1;
constexpr int kLength = 1;
constexpr int kBps = 8;
constexpr int kPhotometric = PHOTOMETRIC_RGB;
constexpr int kRowsPerStrip = 1;
constexpr int kPlanarConfig = PLANARCONFIG_CONTIG;

struct SingleTag {
  const ttag_t tag;
  const uint16_t value;
};

constexpr std::array<SingleTag, 9> kShortSingleTags = {
    SingleTag{TIFFTAG_COMPRESSION, COMPRESSION_NONE},
    SingleTag{TIFFTAG_FILLORDER, FILLORDER_MSB2LSB},
    SingleTag{TIFFTAG_ORIENTATION, ORIENTATION_BOTRIGHT},
    SingleTag{TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH},
    SingleTag{TIFFTAG_MINSAMPLEVALUE, 23},
    SingleTag{TIFFTAG_MAXSAMPLEVALUE, 241},
    SingleTag{TIFFTAG_INKSET, INKSET_MULTIINK},
    SingleTag{TIFFTAG_NUMBEROFINKS, kSamplePerPixel},
    SingleTag{TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT}};

struct PairedTag {
  const ttag_t tag;
  const std::array<uint16_t, 2> values;
};

constexpr std::array<PairedTag, 4> kShortPairedTags = {
    PairedTag{TIFFTAG_PAGENUMBER, {1, 1}},
    PairedTag{TIFFTAG_HALFTONEHINTS, {0, 255}},
    PairedTag{TIFFTAG_DOTRANGE, {8, 16}},
    PairedTag{TIFFTAG_YCBCRSUBSAMPLING, {2, 1}}};

TEST(SandboxTest, ShortTag) {
  absl::StatusOr<std::string> status_or_path =
      sapi::CreateNamedTempFileAndClose("short_test.tif");
  ASSERT_THAT(status_or_path, IsOk()) << "Could not create temp file";

  std::string srcfile =
      sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(), *status_or_path);

  TiffSapiSandbox sandbox("", srcfile);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";

  std::array<uint8_t, kSamplePerPixel> buffer = {0, 127, 255};
  sapi::v::Array<uint8_t> buffer_(buffer.data(), kSamplePerPixel);

  absl::StatusOr<int> status_or_int;
  absl::StatusOr<TIFF*> status_or_tif;

  TiffApi api(&sandbox);
  sapi::v::ConstCStr srcfile_var(srcfile.c_str());
  sapi::v::ConstCStr w_var("w");

  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), w_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "Could not open " << srcfile;

  sapi::v::RemotePtr tif(status_or_tif.value());
  ASSERT_THAT(tif.GetValue(), NotNull())
      << "Can't create test TIFF file " << srcfile;

  status_or_int = api.TIFFSetFieldUShort1(&tif, TIFFTAG_IMAGEWIDTH, kWidth);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set ImagekWidth tag";

  status_or_int = api.TIFFSetFieldUShort1(&tif, TIFFTAG_IMAGELENGTH, kLength);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set ImageLength tag";

  status_or_int = api.TIFFSetFieldUShort1(&tif, TIFFTAG_BITSPERSAMPLE, kBps);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set BitsPerSample tag";

  status_or_int =
      api.TIFFSetFieldUShort1(&tif, TIFFTAG_SAMPLESPERPIXEL, kSamplePerPixel);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set SamplesPerPixel tag";

  status_or_int =
      api.TIFFSetFieldUShort1(&tif, TIFFTAG_ROWSPERSTRIP, kRowsPerStrip);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set RowsPerStrip tag";

  status_or_int =
      api.TIFFSetFieldUShort1(&tif, TIFFTAG_PLANARCONFIG, kPlanarConfig);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set PlanarConfiguration tag";

  status_or_int =
      api.TIFFSetFieldUShort1(&tif, TIFFTAG_PHOTOMETRIC, kPhotometric);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set PhotometricInterpretation tag";

  for (auto& tag : kShortSingleTags) {
    status_or_int = api.TIFFSetFieldUShort1(&tif, tag.tag, tag.value);
    ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort1 fatal error";
    EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set tag " << tag.tag;
  }

  for (auto& tag : kShortPairedTags) {
    status_or_int =
        api.TIFFSetFieldUShort2(&tif, tag.tag, tag.values[0], tag.values[1]);
    ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldUShort2 fatal error";
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
  CheckShortField(api, tif2, TIFFTAG_BITSPERSAMPLE, kBps);
  CheckShortField(api, tif2, TIFFTAG_PHOTOMETRIC, kPhotometric);
  CheckShortField(api, tif2, TIFFTAG_SAMPLESPERPIXEL, kSamplePerPixel);
  CheckLongField(api, tif2, TIFFTAG_ROWSPERSTRIP, kRowsPerStrip);
  CheckShortField(api, tif2, TIFFTAG_PLANARCONFIG, kPlanarConfig);

  for (auto& tag : kShortSingleTags) {
    CheckShortField(api, tif2, tag.tag, tag.value);
  }

  for (auto& tag : kShortPairedTags) {
    CheckShortPairedField(api, tif2, tag.tag, tag.values);
  }

  ASSERT_THAT(api.TIFFClose(&tif2), IsOk()) << "TIFFClose fatal error";
}

}  // namespace
