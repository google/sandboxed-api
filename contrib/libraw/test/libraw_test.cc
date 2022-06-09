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

#include "contrib/libraw/sandboxed.h"
#include "contrib/libraw/utils/utils_libraw.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"
#include "sandboxed_api/util/temp_file.h"

namespace {

using ::sapi::IsOk;

const struct TestVariant {
  std::string filename;
  ushort raw_height;
  ushort raw_width;
  int COLOR[4][4];
  int color_values[4][4];
} kTestData[] = {{.filename = "img.raw",
                  .raw_height = 540,
                  .raw_width = 960,
                  .COLOR =
                      {
                          {0, 1, 0, 1},
                          {3, 2, 3, 2},
                          {0, 1, 0, 1},
                          {3, 2, 3, 2},
                      },
                  .color_values = {
                      {548, 1285, 554, 1253},
                      {1290, 789, 1279, 788},
                      {551, 1303, 549, 1253},
                      {1265, 809, 1257, 779},
                  }}};

class LibRawBase : public testing::Test {
 protected:
  std::string GetTestFilePath(const std::string& filename) {
    return sapi::file::JoinPath(test_dir_, filename);
  }

  void SetUp() override;

  const char* test_dir_;
};

class LibRawTestFiles : public LibRawBase,
                        public testing::WithParamInterface<TestVariant> {};

void LibRawBase::SetUp() {
  test_dir_ = getenv("TEST_FILES_DIR");
  ASSERT_NE(test_dir_, nullptr);
}

TEST_P(LibRawTestFiles, TestOpen) {
  const TestVariant& tv = GetParam();
  std::string test_file_path = GetTestFilePath(tv.filename);

  LibRawSapiSandbox sandbox(test_file_path);
  SAPI_ASSERT_OK(sandbox.Init());

  LibRaw lr(&sandbox, test_file_path);
  SAPI_ASSERT_OK(lr.CheckIsInit());
  SAPI_ASSERT_OK(lr.OpenFile());
}

TEST_P(LibRawTestFiles, TestUnpack) {
  const TestVariant& tv = GetParam();
  std::string test_file_path = GetTestFilePath(tv.filename);

  LibRawSapiSandbox sandbox(test_file_path);
  SAPI_ASSERT_OK(sandbox.Init());

  LibRaw lr(&sandbox, test_file_path);
  SAPI_ASSERT_OK(lr.CheckIsInit());
  SAPI_ASSERT_OK(lr.OpenFile());
  SAPI_ASSERT_OK(lr.Unpack());
}

TEST_P(LibRawTestFiles, TestSize) {
  const TestVariant& tv = GetParam();
  std::string test_file_path = GetTestFilePath(tv.filename);

  LibRawSapiSandbox sandbox(test_file_path);
  SAPI_ASSERT_OK(sandbox.Init());

  LibRaw lr(&sandbox, test_file_path);
  SAPI_ASSERT_OK(lr.CheckIsInit());
  SAPI_ASSERT_OK(lr.OpenFile());
  SAPI_ASSERT_OK(lr.Unpack());

  SAPI_ASSERT_OK_AND_ASSIGN(ushort raw_height, lr.GetRawHeight());
  SAPI_ASSERT_OK_AND_ASSIGN(ushort raw_width, lr.GetRawWidth());

  EXPECT_EQ(raw_height, tv.raw_height);
  EXPECT_EQ(raw_width, tv.raw_width);
}

TEST_P(LibRawTestFiles, TestCameraList) {
  const TestVariant& tv = GetParam();
  std::string test_file_path = GetTestFilePath(tv.filename);

  LibRawSapiSandbox sandbox(test_file_path);
  SAPI_ASSERT_OK(sandbox.Init());

  LibRaw lr(&sandbox, test_file_path);
  SAPI_ASSERT_OK(lr.CheckIsInit());

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<char*> camera_list, lr.GetCameraList());

  EXPECT_FALSE(camera_list.empty());
}

TEST_P(LibRawTestFiles, TestColor) {
  const TestVariant& tv = GetParam();
  std::string test_file_path = GetTestFilePath(tv.filename);

  LibRawSapiSandbox sandbox(test_file_path);
  SAPI_ASSERT_OK(sandbox.Init());

  LibRaw lr(&sandbox, test_file_path);
  SAPI_ASSERT_OK(lr.CheckIsInit());
  SAPI_ASSERT_OK(lr.OpenFile());
  SAPI_ASSERT_OK(lr.Unpack());

  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      SAPI_ASSERT_OK_AND_ASSIGN(int color, lr.COLOR(row, col));
      EXPECT_EQ(color, tv.COLOR[row][col]);
    }
  }
}

TEST_P(LibRawTestFiles, TestSubtractBlack) {
  const TestVariant& tv = GetParam();
  std::string test_file_path = GetTestFilePath(tv.filename);

  LibRawSapiSandbox sandbox(test_file_path);
  SAPI_ASSERT_OK(sandbox.Init());

  LibRaw lr(&sandbox, test_file_path);
  SAPI_ASSERT_OK(lr.CheckIsInit());
  SAPI_ASSERT_OK(lr.OpenFile());
  SAPI_ASSERT_OK(lr.Unpack());
  SAPI_ASSERT_OK(lr.SubtractBlack());

  libraw_data_t lr_data = lr.GetImgData();

  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<uint16_t> rawdata, lr.RawData());

  for (int row = 0; row < 4; ++row) {
    unsigned rcolors[48];
    if (lr_data.idata.colors > 1) {
      for (int c = 0; c < 48; c++) {
        SAPI_ASSERT_OK_AND_ASSIGN(int color, lr.COLOR(row, c));
        rcolors[c] = color;
      }
    } else {
      memset(rcolors, 0, sizeof(rcolors));
    }

    for (int col = 0; col < 4; col++) {
      int raw_idx = row * lr.GetImgData().sizes.raw_pitch / 2 + col;
      unsigned black_level = lr_data.color.cblack[rcolors[col % 48]];
      int color_value =
          rawdata[raw_idx] > black_level ? rawdata[raw_idx] - black_level : 0;
      EXPECT_EQ(color_value, tv.color_values[row][col]);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(LibRawBase, LibRawTestFiles,
                         testing::ValuesIn(kTestData));

}  // namespace
