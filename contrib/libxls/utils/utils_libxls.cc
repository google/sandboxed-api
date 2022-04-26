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

#include "contrib/libxls/utils/utils_libxls.h"

#include <fstream>
#include <iostream>
#include <string>

#include "contrib/libxls/sandboxed.h"

absl::Status GetError(LibxlsApi* api, xls_error_t error_code) {
  SAPI_ASSIGN_OR_RETURN(const char* c_errstr, api->xls_getError(error_code));
  sapi::v::RemotePtr sapi_errstr(const_cast<char*>(c_errstr));
  SAPI_ASSIGN_OR_RETURN(std::string errstr,
                        api->GetSandbox()->GetCString(sapi_errstr));

  return absl::UnavailableError(errstr);
}

absl::StatusOr<LibXlsWorkbook> LibXlsWorkbook::Open(LibxlsSapiSandbox* sandbox,
                                                    const std::string& filename,
                                                    const std::string& encode) {
  if (sandbox == nullptr) {
    return absl::InvalidArgumentError("Sandbox has to be defined");
  }

  LibxlsApi api(sandbox);

  sapi::v::IntBase<xls_error_t> sapi_error;
  sapi::v::CStr sapi_filename(filename.c_str());
  sapi::v::CStr sapi_encode(encode.c_str());

  SAPI_ASSIGN_OR_RETURN(
      xlsWorkBook * wb,
      api.xls_open_file(sapi_filename.PtrBefore(), sapi_encode.PtrBefore(),
                        sapi_error.PtrAfter()));

  if (wb == nullptr) {
    return GetError(&api, sapi_error.GetValue());
  }

  sapi::v::Struct<xlsWorkBook> sapi_wb;
  sapi_wb.SetRemote(wb);
  SAPI_RETURN_IF_ERROR(sandbox->TransferFromSandboxee(&sapi_wb));

  return LibXlsWorkbook(sandbox, wb, sapi_wb.data().sheets.count);
}

LibXlsWorkbook::~LibXlsWorkbook() {
  if (rwb_ != nullptr) {
    sapi::v::RemotePtr sapi_rwb(rwb_);
    LibxlsApi api(sandbox_);
    api.xls_close_WB(&sapi_rwb).IgnoreError();
  }
}

size_t LibXlsWorkbook::GetSheetCount() { return sheet_count_; }

absl::StatusOr<LibXlsSheet> LibXlsWorkbook::OpenSheet(uint32_t index) {
  if (GetSheetCount() <= index) {
    return absl::OutOfRangeError("Index out of range");
  }

  LibxlsApi api(sandbox_);
  sapi::v::RemotePtr sapi_rwb(rwb_);
  SAPI_ASSIGN_OR_RETURN(xlsWorkSheet * ws,
                        api.xls_getWorkSheet(&sapi_rwb, index));
  if (ws == nullptr) {
    return absl::UnavailableError("Unable to open sheet");
  }

  sapi::v::Struct<xlsWorkSheet> sapi_ws;
  sapi_ws.SetRemote(ws);
  SAPI_ASSIGN_OR_RETURN(xls_error_t error_code,
                        api.xls_parseWorkSheet(sapi_ws.PtrAfter()));
  if (error_code != 0) {
    return GetError(&api, error_code);
  }

  return LibXlsSheet(sandbox_, ws, sapi_ws.data().rows.lastrow + 1,
                     sapi_ws.data().rows.lastcol + 1);
}

size_t LibXlsSheet::GetRowCount() const { return row_; }

size_t LibXlsSheet::GetColCount() const { return col_; }

absl::StatusOr<std::string> LibXlsSheet::GetStr(
    const sapi::v::Struct<xlsCell>& sapi_cell) {
  if (sapi_cell.data().str == nullptr) {
    return "";
  }

  sapi::v::RemotePtr sapi_str(sapi_cell.data().str);
  return sandbox_->GetCString(sapi_str);
}

absl::StatusOr<LibXlsCell> LibXlsSheet::GetNewCell(
    const sapi::v::Struct<xlsCell>& sapi_cell) {
  int id = sapi_cell.data().id;
  double d = sapi_cell.data().d;

  switch (id) {
    case XLS_RECORD_RK:
    case XLS_RECORD_MULRK:
    case XLS_RECORD_NUMBER:
      return LibXlsCell{XLS_RECORD_NUMBER, d};
    case XLS_RECORD_BLANK:
      return LibXlsCell{XLS_RECORD_BLANK, 0.0};
    case XLS_RECORD_FORMULA:
      SAPI_ASSIGN_OR_RETURN(std::string cell_str, GetStr(sapi_cell));
      if (cell_str == "bool") {
        return LibXlsCell{XLS_RECORD_BOOL, d > 0};
      } else if (cell_str == "error") {
        return LibXlsCell{XLS_RECORD_ERROR, cell_str};
      }
      return LibXlsCell{XLS_RECORD_STRING, cell_str};
  }

  return absl::UnavailableError("Unknown type");
}

absl::StatusOr<LibXlsCell> LibXlsSheet::GetCell(uint32_t row, uint32_t col) {
  if (row >= GetRowCount()) {
    return absl::OutOfRangeError("Row out of range");
  }
  if (col >= GetColCount()) {
    return absl::OutOfRangeError("Col out of range");
  }

  LibxlsApi api(sandbox_);
  sapi::v::RemotePtr sapi_rws(rws_);
  SAPI_ASSIGN_OR_RETURN(xlsCell * cell, api.xls_cell(&sapi_rws, row, col));
  if (cell == nullptr) {
    return absl::UnavailableError("Unable to get cell");
  }
  sapi::v::Struct<xlsCell> sapi_cell;
  sapi_cell.SetRemote(cell);
  SAPI_RETURN_IF_ERROR(sandbox_->TransferFromSandboxee(&sapi_cell));

  return GetNewCell(sapi_cell);
}

LibXlsSheet::~LibXlsSheet() {
  if (rws_ != nullptr) {
    LibxlsApi api(sandbox_);
    sapi::v::RemotePtr sapi_rws(rws_);
    api.xls_close_WS(&sapi_rws).IgnoreError();
  }
}
