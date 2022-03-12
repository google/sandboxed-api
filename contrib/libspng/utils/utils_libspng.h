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

#ifndef CONTRIB_LIBSPNG_UTILS_UTILS_LIBSPNG_H_
#define CONTRIB_LIBSPNG_UTILS_UTILS_LIBSPNG_H_

#include <vector>

#include "contrib/libspng/sandboxed.h"
#include "sandboxed_api/util/status_macros.h"

enum spng_format {
  SPNG_FMT_RGBA8 = 1,
  SPNG_FMT_RGBA16 = 2,
  SPNG_FMT_RGB8 = 4,

  SPNG_FMT_GA8 = 16,
  SPNG_FMT_GA16 = 32,
  SPNG_FMT_G8 = 64,

  SPNG_FMT_PNG = 256,
  SPNG_FMT_RAW = 512
};

enum spng_decode_flags {
  SPNG_DECODE_NONE = 0,

  SPNG_DECODE_TRNS = 1,
  SPNG_DECODE_GAMMA = 2,
  SPNG_DECODE_PROGRESSIVE = 256
};

enum spng_color_type {
  SPNG_COLOR_TYPE_GRAYSCALE = 0,
  SPNG_COLOR_TYPE_TRUECOLOR = 2,
  SPNG_COLOR_TYPE_INDEXED = 3,
  SPNG_COLOR_TYPE_GRAYSCALE_ALPHA = 4,
  SPNG_COLOR_TYPE_TRUECOLOR_ALPHA = 6
};

enum spng_encode_flags
{
    SPNG_ENCODE_PROGRESSIVE = 1,
    SPNG_ENCODE_FINALIZE = 2,
};

#define SPNG_CTX_ENCODER 2

#define SPNG_EOI 75

constexpr size_t kMaxBuf = 1024 * 1024 * 1024;  // 1GB

class LibSPng {
 public:
  LibSPng(LibspngSandbox* sandbox, int flags)
      : sandbox_(CHECK_NOTNULL(sandbox)), api_(sandbox_), decode_eof_(false) {
    status_ = InitStruct(flags);
  }
  ~LibSPng();

  bool IsInit();
  absl::Status SetBuffer(std::vector<uint8_t>& buf);

  absl::StatusOr<size_t> GetDecodeSize(enum spng_format fmt);
  absl::StatusOr<std::vector<uint8_t>> Decode(
      enum spng_format fmt, enum spng_decode_flags flags = SPNG_DECODE_NONE);

  absl::StatusOr<std::pair<uint32_t, uint32_t>> GetImageSize();
  absl::StatusOr<uint8_t> GetImageBitDepth();

  absl::StatusOr<struct spng_row_info> GetRowInfo();
  absl::StatusOr<std::vector<uint8_t>> DecodeRow(size_t row_size);
  absl::StatusOr<size_t> GetDecodeRowSize(enum spng_format fmt);

  absl::StatusOr<struct spng_ihdr> GetIHdr();
  absl::Status SetIHdr(struct spng_ihdr ihdr);

  absl::StatusOr<int> GetOption(enum spng_option option);
  absl::Status SetOption(enum spng_option option, int value);

  absl::Status Encode(std::vector<uint8_t>& buf, int fmt, int flags);
  absl::Status EncodeProgressive(int fmt, int flags);
  absl::Status EncodeRow(std::vector<uint8_t>& buf);
  absl::StatusOr<std::vector<uint8_t>> GetPngBuffer();

  absl::Status SetFd(int fd, const std::string& mode);

  bool DecodeEOF();
  void Close();

 protected:
  std::string GetError(int err);
  absl::Status RetError(const std::string& str, int ret);

  absl::Status CheckInit();
  absl::Status CheckTransfered();

  absl::StatusOr<std::vector<uint8_t>> DecodeProgressive(
      enum spng_format fmt, enum spng_decode_flags flags);
  absl::StatusOr<std::vector<uint8_t>> DecodeStandard(
      enum spng_format fmt, enum spng_decode_flags flags);

  absl::Status EncodeStandard(
    std::vector<uint8_t>& buf, int fmt, int flags);

 private:
  absl::Status InitStruct(int flags);

  LibspngSandbox* sandbox_;
  LibspngApi api_;
  absl::Status status_;
  sapi::v::GenericPtr context_;
  sapi::v::GenericPtr bufptr_;
  sapi::v::GenericPtr pfile_;
  bool decode_eof_;
  sapi::v::NullPtr null_ptr_;
};

#endif  // CONTRIB_LIBSPNG_UTILS_UTILS_LIBSPNG_H_
