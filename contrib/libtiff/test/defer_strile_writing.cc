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

#include <cstdint>

#include "helper.h"  // NOLINT(build/include)
#include "tiffio.h"  // NOLINT(build/include)

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::NotNull;

// sapi functions:
// TIFFOpen
// TIFFClose
// TIFFGetField
// TIFFSetField
// TIFFWriteCheck
// TIFFSetDirectory
// TIFFFreeDirectory
// TIFFWriteScanline
// TIFFWriteDirectory
// TIFFCreateDirectory
// TIFFReadEncodedTile
// TIFFReadEncodedStrip
// TIFFWriteEncodedTile
// TIFFWriteEncodedStrip
// TIFFDeferStrileArrayWriting
// TIFFForceStrileArrayWriting

namespace {

constexpr int kTileBufferSize = 256;
constexpr int kWidth = 1;
constexpr int kBps = 8;
constexpr int kRowsPerStrip = 1;
constexpr int kSamplePerPixel = 1;

void TestWriting(const char* mode, int tiled, int height) {
  absl::StatusOr<std::string> status_or_path =
      sapi::CreateNamedTempFileAndClose("defer_strile_writing.tif");
  ASSERT_THAT(status_or_path, IsOk()) << "Could not create temp file";

  std::string srcfile = sapi::file::JoinPath(sapi::file_util::fileops::GetCWD(),
                                             status_or_path.value());

  absl::StatusOr<int> status_or_int;
  absl::StatusOr<int64_t> status_or_long;
  absl::StatusOr<TIFF*> status_or_tif;

  TiffSapiSandbox sandbox("", srcfile);
  ASSERT_THAT(sandbox.Init(), IsOk()) << "Couldn't initialize Sandboxed API";

  TiffApi api(&sandbox);
  sapi::v::ConstCStr srcfile_var(srcfile.c_str());
  sapi::v::ConstCStr mode_var(mode);

  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), mode_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "Could not open " << srcfile;

  sapi::v::RemotePtr tif(status_or_tif.value());
  ASSERT_THAT(tif.GetValue(), NotNull())
      << "Can't create test TIFF file " << srcfile;

  status_or_int =
      api.TIFFSetFieldU1(&tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set CompressionNone tag";

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_IMAGEWIDTH, kWidth);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set ImageWidth tag";

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_IMAGELENGTH, height);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set ImageLenght tag";

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_BITSPERSAMPLE, kBps);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set BitsPerSample tag";

  status_or_int =
      api.TIFFSetFieldU1(&tif, TIFFTAG_SAMPLESPERPIXEL, kSamplePerPixel);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set SamplesPerPixel tag";

  status_or_int =
      api.TIFFSetFieldU1(&tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set PlanarConfiguration tag";

  if (tiled) {
    status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_TILEWIDTH, 16);
    ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
    EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set TileWidth tag";

    status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_TILELENGTH, 16);
    ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
    EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set TileLenght tag";
  } else {
    status_or_int =
        api.TIFFSetFieldU1(&tif, TIFFTAG_ROWSPERSTRIP, kRowsPerStrip);
    ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
    EXPECT_THAT(status_or_int.value(), IsTrue())
        << "Can't set RowsPerStrip tag";
  }

  status_or_int = api.TIFFDeferStrileArrayWriting(&tif);
  ASSERT_THAT(status_or_int, IsOk())
      << "TIFFDeferStrileArrayWriting fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFDeferStrileArrayWriting return unexpected value";

  sapi::v::ConstCStr test_var("test");
  status_or_int = api.TIFFWriteCheck(&tif, tiled, test_var.PtrBefore());
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFWriteCheck fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFWriteCheck return unexpected value "
      << "void test(" << mode << ", " << tiled << ", " << height << ")";

  status_or_int = api.TIFFWriteDirectory(&tif);
  ASSERT_THAT(status_or_int, IsOk())
      << "TIFFDeferStrileArrayWriting fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFDeferStrileArrayWriting return unexpected value";

  // Create other directory
  ASSERT_THAT(api.TIFFFreeDirectory(&tif), IsOk())
      << "TIFFFreeDirectory fatal error";
  ASSERT_THAT(api.TIFFCreateDirectory(&tif), IsOk())
      << "TIFFCreateDirectory fatal error";

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set SubFileType tag";

  status_or_int =
      api.TIFFSetFieldU1(&tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set CompressionNone tag";

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_IMAGEWIDTH, kWidth);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set ImageWidth tag";

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_IMAGELENGTH, 1);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set ImageLenght tag";

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_BITSPERSAMPLE, kBps);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set BitsPerSample tag";

  status_or_int =
      api.TIFFSetFieldU1(&tif, TIFFTAG_SAMPLESPERPIXEL, kSamplePerPixel);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set SamplesPerPixel tag";

  status_or_int =
      api.TIFFSetFieldU1(&tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Can't set PlanarConfiguration tag";

  status_or_int = api.TIFFSetFieldU1(&tif, TIFFTAG_ROWSPERSTRIP, kRowsPerStrip);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetFieldU1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue()) << "Can't set RowsPerStrip tag";

  status_or_int = api.TIFFDeferStrileArrayWriting(&tif);
  ASSERT_THAT(status_or_int, IsOk())
      << "TIFFDeferStrileArrayWriting fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFDeferStrileArrayWriting return unexpected value";

  status_or_int = api.TIFFWriteCheck(&tif, 0, test_var.PtrBefore());
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFWriteCheck fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFWriteCheck return unexpected value";

  status_or_int = api.TIFFWriteDirectory(&tif);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFWriteDirectory fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFWriteDirectory return unexpected value";

  // Force writing of strile arrays
  status_or_int = api.TIFFSetDirectory(&tif, 0);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetDirectory fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFSetDirectory return unexpected value";

  status_or_int = api.TIFFForceStrileArrayWriting(&tif);
  ASSERT_THAT(status_or_int, IsOk())
      << "TIFFForceStrileArrayWriting fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFForceStrileArrayWriting return unexpected value";

  status_or_int = api.TIFFSetDirectory(&tif, 1);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetDirectory fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFSetDirectory return unexpected value";

  status_or_int = api.TIFFForceStrileArrayWriting(&tif);
  ASSERT_THAT(status_or_int, IsOk())
      << "TIFFForceStrileArrayWriting fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFForceStrileArrayWriting return unexpected value";

  // Now write data on frist directory
  status_or_int = api.TIFFSetDirectory(&tif, 0);
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFSetDirectory fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "TIFFSetDirectory return unexpected value";

  if (tiled) {
    for (int i = 0; i < (height + 15) / 16; ++i) {
      std::array<uint8_t, kTileBufferSize> tilebuffer;
      tilebuffer.fill(i);
      sapi::v::Array<uint8_t> tilebuffer_(tilebuffer.data(), kTileBufferSize);

      status_or_int = api.TIFFWriteEncodedTile(&tif, i, tilebuffer_.PtrBoth(),
                                               kTileBufferSize);
      ASSERT_THAT(status_or_int, IsOk()) << "TIFFWriteEncodedTile fatal error";
      EXPECT_THAT(status_or_int.value(), Eq(kTileBufferSize))
          << "line " << i << ": expected 256, got " << status_or_int.value();
    }
  } else {
    for (int i = 0; i < height; ++i) {
      sapi::v::UChar c(i);
      status_or_long = api.TIFFWriteEncodedStrip(&tif, i, c.PtrBoth(), 1);
      ASSERT_THAT(status_or_long, IsOk())
          << "TIFFWriteEncodedStrip fatal error";
      EXPECT_THAT(status_or_int.value(), Eq(1))
          << "line " << i << ": expected 1, got " << status_or_int.value();

      if (i == 1 && height > 100000) {
        i = height - 2;
      }
    }
  }

  ASSERT_THAT(api.TIFFClose(&tif), IsOk()) << "TIFFClose fatal error";

  sapi::v::ConstCStr r_var("r");
  status_or_tif = api.TIFFOpen(srcfile_var.PtrBefore(), r_var.PtrBefore());
  ASSERT_THAT(status_or_tif, IsOk()) << "Could not open " << srcfile;

  sapi::v::RemotePtr tif2(status_or_tif.value());
  ASSERT_THAT(tif2.GetValue(), NotNull()) << "can't open " << srcfile;

  if (tiled) {
    for (int i = 0; i < (height + 15) / 16; ++i) {
      for (int retry = 0; retry < 2; ++retry) {
        std::array<uint8_t, kTileBufferSize> tilebuffer;
        auto expected_c = static_cast<uint8_t>(i);
        tilebuffer.fill(0);

        sapi::v::Array<uint8_t> tilebuffer_(tilebuffer.data(), kTileBufferSize);
        status_or_long = api.TIFFReadEncodedTile(
            &tif2, i, tilebuffer_.PtrBoth(), kTileBufferSize);
        ASSERT_THAT(status_or_long, IsOk())
            << "TIFFReadEncodedTile fatal error";
        EXPECT_THAT(status_or_long.value(), Eq(kTileBufferSize))
            << "line " << i << ": expected 256, got " << status_or_long.value();

        bool cmp = tilebuffer[0] != expected_c || tilebuffer[255] != expected_c;

        EXPECT_THAT(tilebuffer[0], Eq(expected_c))
            << "unexpected value at tile " << i << ": expected " << expected_c
            << ", got " << tilebuffer[0];

        EXPECT_THAT(tilebuffer[255], Eq(expected_c))
            << "unexpected value at tile " << i << ": expected " << expected_c
            << ", got " << tilebuffer[255];
        if (cmp) {
          ASSERT_THAT(api.TIFFClose(&tif2), IsOk()) << "TIFFClose fatal error";
        }
      }
    }
  } else {
    for (int i = 0; i < height; ++i) {
      for (int retry = 0; retry < 2; ++retry) {
        sapi::v::UChar c(0);
        auto expected_c = static_cast<uint8_t>(i);

        status_or_long = api.TIFFReadEncodedStrip(&tif2, i, c.PtrBoth(), 1);
        ASSERT_THAT(status_or_long, IsOk())
            << "TIFFReadEncodedStrip fatal error";
        EXPECT_THAT(status_or_long.value(), Eq(1))
            << "line " << i << ": expected 1, got " << status_or_long.value();
        EXPECT_THAT(c.GetValue(), Eq(expected_c))
            << "unexpected value at line " << i << ": expected " << expected_c
            << ", got " << c.GetValue();

        if (c.GetValue() != expected_c) {
          ASSERT_THAT(api.TIFFClose(&tif2), IsOk()) << "TIFFClose fatal error";
        }
      }
    }
  }

  ASSERT_THAT(api.TIFFClose(&tif2), IsOk()) << "TIFFClose fatal error";

  unlink(srcfile.c_str());
}

TEST(SandboxTest, DeferStrileWriting) {
  for (int tiled = 0; tiled <= 1; ++tiled) {
    TestWriting("w", tiled, 1);
    TestWriting("w", tiled, 10);
    TestWriting("w8", tiled, 1);
    TestWriting("wD", tiled, 1);
  }
}

}  // namespace
