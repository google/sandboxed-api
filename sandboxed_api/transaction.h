// Copyright 2020 Google LLC. All Rights Reserved.
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

#ifndef SANDBOXED_API_TRANSACTION_H_
#define SANDBOXED_API_TRANSACTION_H_

#include <memory>

#include <glog/logging.h>
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox.h"
#include "sandboxed_api/util/canonical_errors.h"
#include "sandboxed_api/util/status.h"
#include "sandboxed_api/util/status_macros.h"

#define TRANSACTION_FAIL_IF_NOT(x, y)        \
  if (!(x)) {                                \
    return sapi::FailedPreconditionError(y); \
  }

namespace sapi {

// The Transaction class allows to perform operations in the sandboxee,
// repeating them if necessary (if the sandboxing, or IPC failed).
//
// We provide two different implementations of transactions:
//  1) Single function transactions - They consist out of a single function
//     Main() that will be invoked as body of the transaction. For this,
//     inherit from the Transaction class and implement Main().
//  2) Function pointer based transactions - The BasicTransaction class accepts
//     functions that take a sandbox object (along with arbitrary other
//     parameters) and return a status. This way no custom implementation of a
//     Transaction class is required.
//
// Additionally both methods support Init() and Finish() functions.
// Init() will be called after the sandbox has been set up.
// Finish() will be called when the transaction object goes out of scope.
class TransactionBase {
 public:
  TransactionBase(const TransactionBase&) = delete;
  TransactionBase& operator=(const TransactionBase&) = delete;
  virtual ~TransactionBase();

  // Getter/Setter for retry_cnt_.
  int GetRetryCnt() const { return retry_cnt_; }
  void SetRetryCnt(int retry_count) {
    CHECK_GE(retry_count, 0);
    retry_cnt_ = retry_count;
  }

  // Getter/Setter for time_limit_.
  time_t GetTimeLimit() const { return time_limit_; }
  void SetTimeLimit(time_t time_limit) { time_limit_ = time_limit; }
  void SetTimeLimit(absl::Duration time_limit) {
    time_limit_ = absl::ToTimeT(absl::UnixEpoch() + time_limit);
  }

  // Getter/Setter for inited_.
  bool GetInited() const { return inited_; }
  void SetInited(bool inited) { inited_ = inited; }

  // Getter for the sandbox_.
  Sandbox* GetSandbox() { return sandbox_.get(); }

  // Restarts the sandbox.
  // WARNING: This will invalidate any references to the remote process, make
  // sure you don't keep any var's or FD's to the remote process when calling
  // this.
  sapi::Status Restart() {
    if (inited_) {
      Finish().IgnoreError();
      inited_ = false;
    }
    return sandbox_->Restart(true);
  }

 protected:
  explicit TransactionBase(std::unique_ptr<Sandbox> sandbox)
      : retry_cnt_(kDefaultRetryCnt),
        time_limit_(absl::ToTimeT(absl::UnixEpoch() + kDefaultTimeLimit)),
        inited_(false),
        sandbox_(std::move(sandbox)) {}

  // Runs the main (retrying) transaction loop.
  sapi::Status RunTransactionLoop(const std::function<sapi::Status()>& f);

 private:
  // Number of default transaction execution re-tries, in case of failures.
  static constexpr int kDefaultRetryCnt = 1;

  // Wall-time limit for a single transaction execution (60 s.).
  static constexpr absl::Duration kDefaultTimeLimit = absl::Seconds(60);

  // Executes a single function in the sandbox, used in the main transaction
  // loop. Asserts that the sandbox has been set up and Init() was called.
  sapi::Status RunTransactionFunctionInSandbox(
      const std::function<sapi::Status()>& f);

  // Initialization routine of the sandboxed process that ill be called only
  // once upon sandboxee startup.
  virtual sapi::Status Init() { return sapi::OkStatus(); }

  // End routine for the sandboxee that gets calls when the transaction is
  // destroyed/restarted to clean up resources.
  virtual sapi::Status Finish() { return sapi::OkStatus(); }

  // Number of tries this transaction will be re-executed until it succeeds.
  int retry_cnt_;

  // Time (wall-time) limit for a single Run() call (in seconds). 0 means: no
  // wall-time limit.
  time_t time_limit_;

  // Has Init() finished with success?
  bool inited_;

  // The main sapi::Sandbox object.
  std::unique_ptr<Sandbox> sandbox_;
};

// Regular style transactions, based on inheriting.
class Transaction : public TransactionBase {
 public:
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  using TransactionBase::TransactionBase;

  // Run the transaction.
  sapi::Status Run() {
    return RunTransactionLoop([this] { return Main(); });
  }

 protected:
  // The main sandboxee routine: Can be called multiple times.
  virtual sapi::Status Main() { return sapi::OkStatus(); }
};

// Callback style transactions:
class BasicTransaction final : public TransactionBase {
 private:
  using InitFunction = std::function<sapi::Status(Sandbox*)>;
  using FinishFunction = std::function<sapi::Status(Sandbox*)>;

 public:
  explicit BasicTransaction(std::unique_ptr<Sandbox> sandbox)
      : TransactionBase(std::move(sandbox)),
        init_function_(nullptr),
        finish_function_(nullptr) {}

  template <typename F>
  BasicTransaction(std::unique_ptr<Sandbox> sandbox, F init_function)
      : TransactionBase(std::move(sandbox)),
        init_function_(static_cast<InitFunction>(init_function)),
        finish_function_(nullptr) {}

  template <typename F, typename G>
  BasicTransaction(std::unique_ptr<Sandbox> sandbox, F init_function,
                   G fini_function)
      : TransactionBase(std::move(sandbox)),
        init_function_(static_cast<InitFunction>(init_function)),
        finish_function_(static_cast<FinishFunction>(fini_function)) {}

  // Run any function as body of the transaction that matches our expectations (
  // that is: Returning a sapi::Status and accepting a Sandbox object as first
  // parameter).
  template <typename T, typename... Args>
  sapi::Status Run(T func, Args&&... args) {
    return RunTransactionLoop(
        [&] { return func(GetSandbox(), std::forward<Args>(args)...); });
  }

 private:
  InitFunction init_function_;
  FinishFunction finish_function_;

  sapi::Status Init() final {
    return init_function_ ? init_function_(GetSandbox()) : sapi::OkStatus();
  }

  sapi::Status Finish() final {
    return finish_function_ ? finish_function_(GetSandbox()) : sapi::OkStatus();
  }
};

}  // namespace sapi

#endif  // SANDBOXED_API_TRANSACTION_H_
