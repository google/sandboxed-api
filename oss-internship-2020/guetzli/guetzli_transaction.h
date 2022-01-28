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

#ifndef GUETZLI_SANDBOXED_GUETZLI_TRANSACTION_H_
#define GUETZLI_SANDBOXED_GUETZLI_TRANSACTION_H_

#include <syscall.h>

#include "guetzli_sandbox.h"  // NOLINT(build/include)
#include "absl/status/statusor.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/vars.h"

namespace guetzli::sandbox {

enum class ImageType { kJpeg, kPng };

struct TransactionParams {
  const char* in_file = nullptr;
  const char* out_file = nullptr;
  int verbose = 0;
  int quality = 0;
  int memlimit_mb = 0;
};

// Instance of this transaction shouldn't be reused
// Create a new one for each processing operation
class GuetzliTransaction : public sapi::Transaction {
 public:
  explicit GuetzliTransaction(TransactionParams params, int retry_count = 0)
      : sapi::Transaction(std::make_unique<GuetzliSapiSandbox>()),
        params_(std::move(params)) {
    set_retry_count(retry_count);
    SetTimeLimit(absl::InfiniteDuration());
  }

 private:
  absl::Status Main() final;

  absl::Status LinkOutFile(int out_fd) const;
  absl::StatusOr<ImageType> GetImageTypeFromFd(int fd) const;

  const TransactionParams params_;
  ImageType image_type_ = ImageType::kJpeg;
};

}  // namespace guetzli::sandbox

#endif  // GUETZLI_SANDBOXED_GUETZLI_TRANSACTION_H_
