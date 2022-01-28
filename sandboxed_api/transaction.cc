// Copyright 2019 Google LLC
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

#include "sandboxed_api/transaction.h"

#include "sandboxed_api/util/status_macros.h"

namespace sapi {

constexpr absl::Duration TransactionBase::kDefaultTimeLimit;

absl::Status TransactionBase::RunTransactionFunctionInSandbox(
    const std::function<absl::Status()>& f) {
  // Run Main(), invoking Init() if this hasn't been yet done.
  SAPI_RETURN_IF_ERROR(sandbox_->Init());

  // Set the wall-time limit for this transaction run, and clean it up
  // afterwards, no matter what the result.
  SAPI_RETURN_IF_ERROR(
      sandbox_->SetWallTimeLimit(absl::Seconds(GetTimeLimit())));
  struct TimeCleanup {
    ~TimeCleanup() {
      capture->sandbox_->SetWallTimeLimit(absl::ZeroDuration()).IgnoreError();
    }
    TransactionBase* capture;
  } sandbox_cleanup = {this};

  if (!initialized_) {
    SAPI_RETURN_IF_ERROR(Init());
    initialized_ = true;
  }

  return f();
}

absl::Status TransactionBase::RunTransactionLoop(
    const std::function<absl::Status()>& f) {
  // Try to run Main() for a few times, return error if none of the tries
  // succeeded.
  absl::Status status;
  for (int i = 0; i <= retry_count_; ++i) {
    status = RunTransactionFunctionInSandbox(f);
    if (status.ok()) {
      return status;
    }
    sandbox_->Terminate();
    initialized_ = false;
  }

  LOG(ERROR) << "Tried " << (retry_count_ + 1) << " times to run the "
             << "transaction, but it failed. SAPI error: '" << status
             << "'. Latest sandbox error: '"
             << sandbox_->AwaitResult().ToString() << "'";
  return status;
}

TransactionBase::~TransactionBase() {
  if (!initialized_) {
    return;
  }
  if (absl::Status status = Finish(); !status.ok()) {
    LOG(ERROR) << "Transaction finalizer returned an error: " << status;
  }
}

}  // namespace sapi
