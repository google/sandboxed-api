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

#ifndef CONTRIB_BROTLI_UTILS_UTILS_BROTLI_ENC_H_
#define CONTRIB_BROTLI_UTILS_UTILS_BROTLI_ENC_H_

#include <vector>

#include "absl/log/die_if_null.h"
#include "contrib/brotli/sandboxed.h"
#include "sandboxed_api/util/status_macros.h"

class BrotliEncoder {
 public:
  explicit BrotliEncoder(BrotliSandbox* sandbox)
      : sandbox_(ABSL_DIE_IF_NULL(sandbox)), api_(sandbox_), state_(nullptr) {
    status = InitStructs();
  }
  ~BrotliEncoder();

  bool IsInit();

  absl::Status SetParameter(BrotliEncoderParameter param, uint32_t value);
  absl::Status Compress(std::vector<uint8_t>& buf_in,
                        BrotliEncoderOperation op = BROTLI_OPERATION_FINISH);

  absl::StatusOr<std::vector<uint8_t>> TakeOutput();

 protected:
  absl::Status InitStructs();
  absl::Status CheckIsInit();

  BrotliSandbox* sandbox_;
  BrotliApi api_;
  absl::Status status;
  sapi::v::GenericPtr state_;
  sapi::v::NullPtr null_ptr_;
};

#endif  // CONTRIB_BROTLI_UTILS_UTILS_BROTLI_ENC_H_
