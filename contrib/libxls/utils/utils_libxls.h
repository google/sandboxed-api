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

#ifndef CONTRIB_LIBXLS_UTILS_UTILS_LIBXLS_H_
#define CONTRIB_LIBXLS_UTILS_UTILS_LIBXLS_H_

#include <fcntl.h>

#include "absl/log/die_if_null.h"
#include "contrib/libxls/sandboxed.h"

#define XLS_RECORD_FORMULA 0x0006
#define XLS_RECORD_MULRK 0x00BD
#define XLS_RECORD_BLANK 0x0201
#define XLS_RECORD_NUMBER 0x0203
#define XLS_RECORD_STRING 0x0207
#define XLS_RECORD_RK 0x027E
#define XLS_RECORD_BOOL 0x9998
#define XLS_RECORD_ERROR 0x9999

struct LibXlsCell {
  int type;
  std::variant<double, bool, std::string> value;
};

class LibXlsSheet {
 public:
  size_t GetRowCount() const;
  size_t GetColCount() const;
  absl::StatusOr<LibXlsCell> GetCell(uint32_t row, uint32_t col);

  ~LibXlsSheet();

  LibXlsSheet(LibXlsSheet&& other) { *this = std::move(other); }

  LibXlsSheet& operator=(LibXlsSheet&& other) {
    using std::swap;

    if (this != &other) {
      swap(sandbox_, other.sandbox_);
      swap(rws_, other.rws_);
      swap(row_, other.row_);
      swap(col_, other.col_);
    }
    return *this;
  }

 private:
  friend class LibXlsWorkbook;

  LibXlsSheet(LibxlsSapiSandbox* sandbox, xlsWorkSheet* rws, size_t row,
              size_t col)
      : sandbox_(ABSL_DIE_IF_NULL(sandbox)), rws_(rws), row_(row), col_(col) {}

  absl::StatusOr<std::string> GetStr(const sapi::v::Struct<xlsCell>& sapi_cell);
  absl::StatusOr<LibXlsCell> GetNewCell(
      const sapi::v::Struct<xlsCell>& sapi_cell);

  LibxlsSapiSandbox* sandbox_;
  xlsWorkSheet* rws_ = nullptr;
  size_t row_;
  size_t col_;
};

class LibXlsWorkbook {
 public:
  static absl::StatusOr<LibXlsWorkbook> Open(
      LibxlsSapiSandbox* sandbox, const std::string& filename,
      const std::string& encode = "UTF-8");

  size_t GetSheetCount();
  absl::StatusOr<LibXlsSheet> OpenSheet(uint32_t index);

  ~LibXlsWorkbook();

  LibXlsWorkbook(LibXlsWorkbook&& other) { *this = std::move(other); }

  LibXlsWorkbook& operator=(LibXlsWorkbook&& other) {
    using std::swap;

    if (this != &other) {
      swap(sandbox_, other.sandbox_);
      swap(rwb_, other.rwb_);
      swap(sheet_count_, other.sheet_count_);
    }
    return *this;
  }

 private:
  LibXlsWorkbook(LibxlsSapiSandbox* sandbox, xlsWorkBook* rwb, size_t count)
      : sandbox_(ABSL_DIE_IF_NULL(sandbox)),
        rwb_(ABSL_DIE_IF_NULL(rwb)),
        sheet_count_(count) {}

  LibxlsSapiSandbox* sandbox_;
  xlsWorkBook* rwb_ = nullptr;
  size_t sheet_count_;
};

#endif  // CONTRIB_LIBXLS_UTILS_UTILS_LIBXLS_H_
