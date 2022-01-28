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

#ifndef CONTRIB_JSNONNET_BASE_TRANSACTION_H_
#define CONTRIB_JSNONNET_BASE_TRANSACTION_H_

#include <memory>
#include <string>

#include "jsonnet_base_sandbox.h"  // NOLINT(build/include)

class JsonnetTransaction : public sapi::Transaction {
 public:
  JsonnetTransaction(std::string in_file, std::string out_file)
      : sapi::Transaction(
            std::make_unique<JsonnetBaseSandbox>(in_file, out_file)),
        in_file_(in_file),
        out_file_(out_file) {
    sapi::Transaction::set_retry_count(0);  // Try once, no retries
    sapi::Transaction::SetTimeLimit(0);     // Infinite time limit
  }

 private:
  std::string in_file_;
  std::string out_file_;

  absl::Status Main() override;
};

#endif  // CONTRIB_JSNONNET_BASE_TRANSACTION_H_
