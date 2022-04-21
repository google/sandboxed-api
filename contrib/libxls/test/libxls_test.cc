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

#include "contrib/libxls/sandboxed.h"
#include "contrib/libxls/utils/utils_libxls.h"
#undef FILE  // TODO(cblichmann): Artifact from generated header

#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_matchers.h"

namespace {

using ::sapi::IsOk;
using ::testing::Not;

struct Sheet {
  int count_row;
  int count_col;
  double values[4][4];
};

const struct TestCase {
  std::string filename;
  size_t sheet_count;
  struct Sheet sheet[2];
} kTestData[] = {
    {
        .filename = "t1.xls",
        .sheet_count = 1,
        .sheet =
            {
                {.count_row = 4,
                 .count_col = 2,
                 .values =
                     {
                         {1, 2, 0, 0},
                         {3, 4, 0, 0},
                         {5, 6, 0, 0},
                         {7, 8, 0, 0},
                     }},
            },
    },
    {
        .filename = "t2.xls",
        .sheet_count = 2,
        .sheet =
            {
                {.count_row = 2,
                 .count_col = 3,
                 .values =
                     {
                         {1, 2, 3, 0},
                         {4, 5, 6, 0},
                         {0, 0, 0, 0},
                         {0, 0, 0, 0},
                     }},
                {.count_row = 2,
                 .count_col = 2,
                 .values =
                     {
                         {9, 8, 0, 0},
                         {7, 6, 0, 0},
                         {0, 0, 0, 0},
                         {0, 0, 0, 0},
                     }},
            },
    },
};

class LibXlsBase : public testing::Test {
 protected:
  std::string GetTestFilePath(const std::string& filename) {
    return sapi::file::JoinPath(test_dir_, filename);
  }

  void SetUp() override;

  const char* test_dir_;
};

class LibXlsTestFiles : public LibXlsBase,
                        public testing::WithParamInterface<TestCase> {};

void LibXlsBase::SetUp() {
  test_dir_ = getenv("TEST_FILES_DIR");
  ASSERT_NE(test_dir_, nullptr);
}

TEST_P(LibXlsTestFiles, TestValues) {
  const TestCase& tv = GetParam();
  std::string test_file_path = GetTestFilePath(tv.filename);

  LibxlsSapiSandbox sandbox(test_file_path);
  SAPI_ASSERT_OK(sandbox.Init());

  SAPI_ASSERT_OK_AND_ASSIGN(LibXlsWorkbook wb,
                            LibXlsWorkbook::Open(&sandbox, test_file_path));
  ASSERT_EQ(wb.GetSheetCount(), tv.sheet_count);

  for (int i = 0; i < tv.sheet_count; ++i) {
    SAPI_ASSERT_OK_AND_ASSIGN(LibXlsSheet sheet, wb.OpenSheet(i));
    ASSERT_EQ(sheet.GetRowCount(), tv.sheet[i].count_row);
    ASSERT_EQ(sheet.GetColCount(), tv.sheet[i].count_col);
    for (size_t row = 0; row < sheet.GetRowCount(); ++row) {
      for (size_t col = 0; col < sheet.GetColCount(); ++col) {
        SAPI_ASSERT_OK_AND_ASSIGN(LibXlsCell cell, sheet.GetCell(row, col));
        ASSERT_EQ(cell.type, XLS_RECORD_NUMBER);
        ASSERT_EQ(std::get<double>(cell.value), tv.sheet[i].values[row][col]);
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(LibXlsBase, LibXlsTestFiles,
                         testing::ValuesIn(kTestData));

TEST_F(LibXlsBase, TestFormula) {
  std::string test_file_path = GetTestFilePath("t3.xls");

  LibxlsSapiSandbox sandbox(test_file_path);
  SAPI_ASSERT_OK(sandbox.Init());

  SAPI_ASSERT_OK_AND_ASSIGN(LibXlsWorkbook wb,
                            LibXlsWorkbook::Open(&sandbox, test_file_path));

  SAPI_ASSERT_OK_AND_ASSIGN(LibXlsSheet sheet, wb.OpenSheet(0));
  SAPI_ASSERT_OK_AND_ASSIGN(LibXlsCell cell, sheet.GetCell(0, 0));
  ASSERT_EQ(cell.type, XLS_RECORD_STRING);
  ASSERT_EQ(std::get<std::string>(cell.value), "10.000000");
}

}  // namespace
