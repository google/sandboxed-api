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

#ifndef SANDBOXED_API_TRANSACTION_H_
#define SANDBOXED_API_TRANSACTION_H_

#include <memory>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox.h"

#define TRANSACTION_FAIL_IF_NOT(x, y)        \
  if (!(x)) {                                \
    return absl::FailedPreconditionError(y); \
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

  // Getter/Setter for retry_count_.
  int retry_count() const { return retry_count_; }
  void set_retry_count(int value) {
    CHECK_GE(value, 0);
    retry_count_ = value;
  }

  // Getter/Setter for time_limit_.
  time_t GetTimeLimit() const { return time_limit_; }
  void SetTimeLimit(time_t time_limit) { time_limit_ = time_limit; }
  void SetTimeLimit(absl::Duration time_limit) {
    time_limit_ = absl::ToTimeT(absl::UnixEpoch() + time_limit);
  }

  bool IsInitialized() const { return initialized_; }

  // Getter for the sandbox_.
  Sandbox* sandbox() const { return sandbox_.get(); }

  // Restarts the sandbox.
  // WARNING: This will invalidate any references to the remote process, make
  //          sure you don't keep any vars or FDs to the remote process when
  //          calling this.
  absl::Status Restart() {
    if (initialized_) {
      Finish().IgnoreError();
      initialized_ = false;
    }
    return sandbox_->Restart(true);
  }

 protected:
  explicit TransactionBase(std::unique_ptr<Sandbox> sandbox)
      : time_limit_(absl::ToTimeT(absl::UnixEpoch() + kDefaultTimeLimit)),
        sandbox_(std::move(sandbox)) {}

  // Runs the main (retrying) transaction loop.
  absl::Status RunTransactionLoop(const std::function<absl::Status()>& f);

 private:
  // Number of default transaction execution re-tries, in case of failures.
  static constexpr int kDefaultRetryCount = 1;

  // Wall-time limit for a single transaction execution (60 s.).
  static constexpr absl::Duration kDefaultTimeLimit = absl::Seconds(60);

  // Executes a single function in the sandbox, used in the main transaction
  // loop. Asserts that the sandbox has been set up and Init() was called.
  absl::Status RunTransactionFunctionInSandbox(
      const std::function<absl::Status()>& f);

  // Initialization routine of the sandboxed process that will be called only
  // once upon sandboxee startup.
  virtual absl::Status Init() { return absl::OkStatus(); }

  // End routine for the sandboxee that gets calls when the transaction is
  // destroyed/restarted to clean up resources.
  virtual absl::Status Finish() { return absl::OkStatus(); }

  // Number of tries this transaction will be re-executed until it succeeds.
  int retry_count_ = kDefaultRetryCount;

  // Time (wall-time) limit for a single Run() call (in seconds). 0 means: no
  // wall-time limit.
  time_t time_limit_;

  // Has Init() finished with success?
  bool initialized_ = false;

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
  absl::Status Run() {
    return RunTransactionLoop([this] { return Main(); });
  }

 protected:
  // The main sandboxee routine: Can be called multiple times.
  virtual absl::Status Main() { return absl::OkStatus(); }
};

// Callback style transactions:
class BasicTransaction final : public TransactionBase {
 private:
  using InitFunction = std::function<absl::Status(Sandbox*)>;
  using FinishFunction = std::function<absl::Status(Sandbox*)>;

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

  // Run any function as body of the transaction that matches our expectations
  // (that is: Returning a Status and accepting a Sandbox object as first
  // parameter).
  template <typename T, typename... Args>
  absl::Status Run(T func, Args&&... args) {
    return RunTransactionLoop(
        [&] { return func(sandbox(), std::forward<Args>(args)...); });
  }

 private:
  InitFunction init_function_;
  FinishFunction finish_function_;

  absl::Status Init() final {
    return init_function_ ? init_function_(sandbox()) : absl::OkStatus();
  }

  absl::Status Finish() final {
    return finish_function_ ? finish_function_(sandbox()) : absl::OkStatus();
  }
};

}  // namespace sapi

#endif  // SANDBOXED_API_TRANSACTION_H_
