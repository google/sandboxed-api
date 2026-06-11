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

#ifndef CONTRIB_LIBRAW_UTILS_UTILS_LIBRAW_H_
#define CONTRIB_LIBRAW_UTILS_UTILS_LIBRAW_H_

#include <vector>

#include "absl/log/die_if_null.h"
#include "contrib/libraw/sandboxed.h"

enum LibRaw_errors {
  LIBRAW_SUCCESS = 0,
  LIBRAW_UNSPECIFIED_ERROR = -1,
  LIBRAW_FILE_UNSUPPORTED = -2,
  LIBRAW_REQUEST_FOR_NONEXISTENT_IMAGE = -3,
  LIBRAW_OUT_OF_ORDER_CALL = -4,
  LIBRAW_NO_THUMBNAIL = -5,
  LIBRAW_UNSUPPORTED_THUMBNAIL = -6,
  LIBRAW_INPUT_CLOSED = -7,
  LIBRAW_NOT_IMPLEMENTED = -8,
  LIBRAW_UNSUFFICIENT_MEMORY = -100007,
  LIBRAW_DATA_ERROR = -100008,
  LIBRAW_IO_ERROR = -100009,
  LIBRAW_CANCELLED_BY_CALLBACK = -100010,
  LIBRAW_BAD_CROP = -100011,
  LIBRAW_TOO_BIG = -100012,
  LIBRAW_MEMPOOL_OVERFLOW = -100013
};

class LibRaw {
 public:
  LibRaw(LibRawSapiSandbox* sandbox, const std::string& file_name)
      : sandbox_(ABSL_DIE_IF_NULL(sandbox)),
        api_(sandbox_),
        file_name_(file_name) {
    init_status_ = InitLibRaw();
  }

  ~LibRaw();

  absl::Status CheckIsInit();
  bool IsInit();

  libraw_data_t GetImgData();
  absl::StatusOr<std::vector<uint16_t>> RawData();

  absl::Status OpenFile();
  absl::Status Unpack();
  absl::Status SubtractBlack();
  absl::StatusOr<std::vector<char*>> GetCameraList();
  absl::StatusOr<int> COLOR(int row, int col);
  absl::StatusOr<int> GetRawHeight();
  absl::StatusOr<int> GetRawWidth();
  absl::StatusOr<unsigned int> GetCBlack(int channel);
  int GetColorCount();

 private:
  absl::Status InitLibRaw();

  LibRawSapiSandbox* sandbox_;
  LibRawApi api_;
  absl::Status init_status_;

  std::string file_name_;

  sapi::v::Struct<libraw_data_t> sapi_libraw_data_t_;
};

#endif  // CONTRIB_LIBRAW_UTILS_UTILS_LIBRAW_H_
