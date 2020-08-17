// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GUETZLI_SANDBOXED_GUETZLI_TRANSACTION_H_
#define GUETZLI_SANDBOXED_GUETZLI_TRANSACTION_H_

#include <syscall.h>

#include "sandboxed_api/transaction.h"
#include "sandboxed_api/vars.h"

#include "guetzli_sandbox.h"

namespace guetzli {
namespace sandbox {

enum class ImageType {
  kJpeg,
  kPng
};

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
  GuetzliTransaction(TransactionParams params)
      : sapi::Transaction(std::make_unique<GuetzliSapiSandbox>())
      , params_(std::move(params))
  {
    //TODO: Add retry count as a parameter
    sapi::Transaction::set_retry_count(kDefaultTransactionRetryCount);
    sapi::Transaction::SetTimeLimit(0);  // Infinite time limit
  }

 private:
  //absl::Status Init() override;
  absl::Status Main() final;

  absl::Status LinkOutFile(int out_fd) const;
  sapi::StatusOr<ImageType> GetImageTypeFromFd(int fd) const;

  const TransactionParams params_;
  ImageType image_type_ = ImageType::kJpeg;

  static const int kDefaultTransactionRetryCount = 0;
};

}  // namespace sandbox
}  // namespace guetzli

#endif  // GUETZLI_SANDBOXED_GUETZLI_TRANSACTION_H_
