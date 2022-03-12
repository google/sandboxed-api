// Copyright 2022 Google LLC
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

#include <fstream>

#include <fcntl.h>

#include "contrib/libspng/sandboxed.h"
#include "contrib/libspng/utils/utils.h"
#include "contrib/libspng/utils/utils_libspng.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

namespace {

using ::sapi::IsOk;

const struct TestVariant {
  std::string filename;
  uint32_t width;
  uint32_t height;
  uint8_t bit_depth;
  std::string rgb8_filename;
  size_t rgb8_decode_size;
} TestData[] = {
    {
        .filename = "pngtest.png",
        .width = 91,
        .height = 69,
        .bit_depth = 8,
        .rgb8_filename = "pngtest.rgb",
        .rgb8_decode_size = 18837,
    },
    {
        .filename = "red_ball.png",
        .width = 240,
        .height = 160,
        .bit_depth = 8,
        .rgb8_filename = "red_ball.rgb",
        .rgb8_decode_size = 115200,
    },
};

class LibSPngBase : public testing::Test {
 protected:
  std::string GetTestFilePath(const std::string& filename) {
    return sapi::file::JoinPath(test_dir_, filename);
  }

  void SetUp() override;

  std::unique_ptr<LibspngSapiSandbox> sandbox_;
  const char* test_dir_;
};

class LibSPngTestFiles : public LibSPngBase,
                         public testing::WithParamInterface<TestVariant> {};

void LibSPngBase::SetUp() {
  sandbox_ = std::make_unique<LibspngSapiSandbox>();
  ASSERT_THAT(sandbox_->Init(), IsOk());

  test_dir_ = getenv("TEST_FILES_DIR");
  ASSERT_NE(test_dir_, nullptr);
}

TEST_F(LibSPngBase, InitLib) {
  LibSPng png(sandbox_.get(), 0);

  ASSERT_TRUE(png.IsInit());
}

TEST_F(LibSPngBase, TestSetGetOption) {
  LibSPng png(sandbox_.get(), 0);
  ASSERT_TRUE(png.IsInit());

  int value;

  SAPI_ASSERT_OK_AND_ASSIGN(value, png.GetOption(SPNG_IMG_COMPRESSION_LEVEL));
  ASSERT_THAT(value, -1);
  value = 1;
  ASSERT_THAT(png.SetOption(SPNG_IMG_COMPRESSION_LEVEL, value), IsOk());
  SAPI_ASSERT_OK_AND_ASSIGN(value, png.GetOption(SPNG_IMG_COMPRESSION_LEVEL));
  ASSERT_THAT(value, 1);
}

TEST_F(LibSPngTestFiles, SetIHdr) {
  struct spng_ihdr ihdr_new = {
      .width = 80,
      .height = 70,
      .bit_depth = 8,
      .color_type = SPNG_COLOR_TYPE_GRAYSCALE,
  };

  LibSPng png(sandbox_.get(), SPNG_CTX_ENCODER);
  ASSERT_TRUE(png.IsInit());

  struct spng_ihdr ihdr;
  SAPI_ASSERT_OK_AND_ASSIGN(ihdr, png.GetIHdr());
  ASSERT_THAT(ihdr.width, 0);
  ASSERT_THAT(ihdr.height, 0);
  ASSERT_THAT(ihdr.color_type, 0);
  ASSERT_THAT(ihdr.bit_depth, 0);
  ASSERT_THAT(ihdr.color_type, 0);
  ASSERT_THAT(ihdr.compression_method, 0);
  ASSERT_THAT(ihdr.filter_method, 0);
  ASSERT_THAT(ihdr.interlace_method, 0);

  ASSERT_THAT(png.SetIHdr(ihdr_new), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(ihdr, png.GetIHdr());
  ASSERT_THAT(ihdr.width, ihdr_new.width);
  ASSERT_THAT(ihdr.height, ihdr_new.height);
  ASSERT_THAT(ihdr.color_type, ihdr_new.color_type);
  ASSERT_THAT(ihdr.bit_depth, ihdr_new.bit_depth);
  ASSERT_THAT(ihdr.color_type, ihdr_new.color_type);
  ASSERT_THAT(ihdr.compression_method, ihdr_new.compression_method);
  ASSERT_THAT(ihdr.filter_method, ihdr_new.filter_method);
  ASSERT_THAT(ihdr.interlace_method, ihdr_new.interlace_method);
}

TEST_P(LibSPngTestFiles, CheckLimits) {
  const TestVariant& tv = GetParam();

  LibSPng png(sandbox_.get(), 0);
  ASSERT_TRUE(png.IsInit());

  std::string filename = GetTestFilePath(tv.filename);
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf, ReadFile(filename));

  ASSERT_THAT(png.SetBuffer(buf), IsOk());

  std::pair<uint32_t, uint32_t> limits;
  SAPI_ASSERT_OK_AND_ASSIGN(limits, png.GetImageSize());
  ASSERT_EQ(limits.first, tv.width);
  ASSERT_EQ(limits.second, tv.height);
}

TEST_P(LibSPngTestFiles, GetHdr) {
  const TestVariant& tv = GetParam();

  LibSPng png(sandbox_.get(), 0);
  ASSERT_TRUE(png.IsInit());

  std::string filename = GetTestFilePath(tv.filename);
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf, ReadFile(filename));

  ASSERT_THAT(png.SetBuffer(buf), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(struct spng_ihdr ihdr, png.GetIHdr());

  ASSERT_EQ(ihdr.bit_depth, tv.bit_depth);
  ASSERT_EQ(ihdr.width, tv.width);
  ASSERT_EQ(ihdr.height, tv.height);
}

TEST_P(LibSPngTestFiles, CheckBitDepth) {
  const TestVariant& tv = GetParam();

  LibSPng png(sandbox_.get(), 0);
  ASSERT_TRUE(png.IsInit());

  std::string filename = GetTestFilePath(tv.filename);
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf, ReadFile(filename));

  ASSERT_THAT(png.SetBuffer(buf), IsOk());
  SAPI_ASSERT_OK_AND_ASSIGN(uint8_t bit_depth, png.GetImageBitDepth());

  ASSERT_EQ(bit_depth, tv.bit_depth);
}

TEST_P(LibSPngTestFiles, CheckDecodeSizeRGB8) {
  const TestVariant& tv = GetParam();

  LibSPng png(sandbox_.get(), 0);
  ASSERT_TRUE(png.IsInit());

  std::string filename = GetTestFilePath(tv.filename);
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf, ReadFile(filename));

  ASSERT_THAT(png.SetBuffer(buf), IsOk());
  SAPI_ASSERT_OK_AND_ASSIGN(size_t size, png.GetDecodeSize(SPNG_FMT_RGB8));

  ASSERT_EQ(size, tv.rgb8_decode_size);
}

TEST_P(LibSPngTestFiles, CheckDecodeRGB8) {
  const TestVariant& tv = GetParam();

  LibSPng png(sandbox_.get(), 0);
  ASSERT_TRUE(png.IsInit());

  std::string filename = GetTestFilePath(tv.filename);
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf, ReadFile(filename));

  ASSERT_THAT(png.SetBuffer(buf), IsOk());
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> out_buf,
                            png.Decode(SPNG_FMT_RGB8));
  ASSERT_EQ(out_buf.size(), tv.rgb8_decode_size);

  std::string cmp_filename = GetTestFilePath(tv.rgb8_filename);
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> cmp_buf,
                            ReadFile(cmp_filename));

  ASSERT_THAT(out_buf, cmp_buf);
}

TEST_P(LibSPngTestFiles, CheckDecodeProgressiveRGB8) {
  const TestVariant& tv = GetParam();

  LibSPng png(sandbox_.get(), 0);
  ASSERT_TRUE(png.IsInit());

  std::string filename = GetTestFilePath(tv.filename);
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf, ReadFile(filename));

  std::string cmp_filename = GetTestFilePath(tv.rgb8_filename);
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> cmp_buf,
                            ReadFile(cmp_filename));

  ASSERT_THAT(png.SetBuffer(buf), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> out_buf,
                            png.Decode(SPNG_FMT_RGB8, SPNG_DECODE_PROGRESSIVE));
  ASSERT_THAT(out_buf.empty(), true);

  SAPI_ASSERT_OK_AND_ASSIGN(size_t decodesize,
                            png.GetDecodeSize(SPNG_FMT_RGB8));

  std::pair<uint32_t, uint32_t> limit;
  SAPI_ASSERT_OK_AND_ASSIGN(limit, png.GetImageSize());

  std::vector<uint8_t> out;
  size_t row_size = decodesize / limit.second;
  while (true) {
    SAPI_ASSERT_OK_AND_ASSIGN(struct spng_row_info row_info, png.GetRowInfo());
    if (png.DecodeEOF()) {
      break;
    }
    SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> row,
                              png.DecodeRow(row_size));

    size_t index = row_info.row_num * row_size;
    std::vector cmp_part(cmp_buf.begin() + index,
                         cmp_buf.begin() + index + row_size);
    ASSERT_THAT(row, cmp_part);
  }
}

TEST_P(LibSPngTestFiles, CheckEncodeRGB8) {
  const TestVariant& tv = GetParam();

  LibSPng png(sandbox_.get(), SPNG_CTX_ENCODER);
  ASSERT_TRUE(png.IsInit());

  std::string filename = GetTestFilePath(tv.rgb8_filename);
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf, ReadFile(filename));

  struct spng_ihdr ihdr = {
      .width = tv.width,
      .height = tv.height,
      .bit_depth = 8,
      .color_type = SPNG_COLOR_TYPE_TRUECOLOR,
  };

  ASSERT_THAT(png.SetIHdr(ihdr), IsOk());
  ASSERT_THAT(png.SetOption(SPNG_ENCODE_TO_BUFFER, 1), IsOk());
  ASSERT_THAT(png.Encode(buf, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE), IsOk());
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> out_buf, png.GetPngBuffer());

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf_orig,
                            ReadFile(GetTestFilePath(tv.filename)));

  ASSERT_THAT(out_buf, buf_orig);
}

TEST_P(LibSPngTestFiles, CheckEncodeProgressiveRGB8) {
  const TestVariant& tv = GetParam();

  LibSPng png(sandbox_.get(), SPNG_CTX_ENCODER);
  ASSERT_TRUE(png.IsInit());

  std::string filename = GetTestFilePath(tv.rgb8_filename);
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf, ReadFile(filename));

  struct spng_ihdr ihdr = {
      .width = tv.width,
      .height = tv.height,
      .bit_depth = 8,
      .color_type = SPNG_COLOR_TYPE_TRUECOLOR,
  };

  ASSERT_THAT(png.SetIHdr(ihdr), IsOk());
  ASSERT_THAT(png.SetOption(SPNG_ENCODE_TO_BUFFER, 1), IsOk());
  ASSERT_THAT(png.EncodeProgressive(
                  SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE | SPNG_ENCODE_PROGRESSIVE),
              IsOk());

  size_t row_size = buf.size() / tv.height;
  for (int i = 0; i < tv.height; ++i) {
    size_t index = i * row_size;
    std::vector<uint8_t> row(buf.begin() + index,
                             buf.begin() + index + row_size);
    ASSERT_THAT(png.EncodeRow(row), IsOk());
  }

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> out_buf, png.GetPngBuffer());

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf_orig,
                            ReadFile(GetTestFilePath(tv.filename)));

  ASSERT_THAT(out_buf, buf_orig);
}

TEST_P(LibSPngTestFiles, CheckSetFd) {
  const TestVariant& tv = GetParam();

  LibSPng png(sandbox_.get(), 0);
  ASSERT_TRUE(png.IsInit());

  std::string filename = GetTestFilePath(tv.filename);

  int fd = open(filename.c_str(), O_RDONLY);
  ASSERT_GE(fd, 0);
  ASSERT_THAT(png.SetFd(fd, "r"), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(struct spng_ihdr ihdr, png.GetIHdr());

  ASSERT_EQ(ihdr.bit_depth, tv.bit_depth);
  ASSERT_EQ(ihdr.width, tv.width);
  ASSERT_EQ(ihdr.height, tv.height);
}

TEST_P(LibSPngTestFiles, CheckDecoderRGB8Fd) {
  const TestVariant& tv = GetParam();

  LibSPng png(sandbox_.get(), 0);
  ASSERT_TRUE(png.IsInit());

  std::string filename = GetTestFilePath(tv.filename);
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf, ReadFile(filename));

  int fd = open(filename.c_str(), O_RDONLY);
  ASSERT_GE(fd, 0);
  ASSERT_THAT(png.SetFd(fd, "r"), IsOk());

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> out_buf,
                            png.Decode(SPNG_FMT_RGB8));
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf_orig,
                            ReadFile(GetTestFilePath(tv.rgb8_filename)));

  ASSERT_THAT(out_buf, buf_orig);
}

TEST_P(LibSPngTestFiles, CheckEncodeRGB8Fd) {
  const TestVariant& tv = GetParam();

  LibSPng png(sandbox_.get(), SPNG_CTX_ENCODER);
  ASSERT_TRUE(png.IsInit());

  SAPI_ASSERT_OK_AND_ASSIGN(std::string outfile,
                            sapi::CreateNamedTempFileAndClose("encode.png"));
  std::string filename = GetTestFilePath(tv.rgb8_filename);
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf, ReadFile(filename));

  int fd = open(outfile.c_str(), O_WRONLY);
  ASSERT_GE(fd, 0);
  ASSERT_THAT(png.SetFd(fd, "w"), IsOk());

  struct spng_ihdr ihdr = {
      .width = tv.width,
      .height = tv.height,
      .bit_depth = 8,
      .color_type = SPNG_COLOR_TYPE_TRUECOLOR,
  };

  ASSERT_THAT(png.SetIHdr(ihdr), IsOk());
  ASSERT_THAT(png.Encode(buf, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE), IsOk());

  // Enforce flush
  png.Close();

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf_orig,
                            ReadFile(GetTestFilePath(tv.filename)));
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint8_t> buf_saved,
                            ReadFile(outfile))

  ASSERT_THAT(buf_saved, buf_orig);
}

INSTANTIATE_TEST_SUITE_P(LibSPngBase, LibSPngTestFiles,
                         testing::ValuesIn(TestData));

}  // namespace
