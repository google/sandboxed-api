// Copyright 2026 Google LLC
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

#ifndef SANDBOXED_API_NULL_RPCCHANNEL_H_
#define SANDBOXED_API_NULL_RPCCHANNEL_H_

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "sandboxed_api/call.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/var_type.h"

namespace sapi {

class PassthroughRPCChannel : public RPCChannel {
 public:
  using CallFunctionT =
      absl::AnyInvocable<void(const FuncCall& call, FuncRet* ret) const>;
  using SymbolFunctionT =
      absl::AnyInvocable<void(const char* symname, FuncRet* ret) const>;

  explicit PassthroughRPCChannel(CallFunctionT call_function,
                                 SymbolFunctionT symbol_function)
      : call_function_(std::move(call_function)),
        symbol_function_(std::move(symbol_function)) {}

  absl::Status Allocate(size_t size, void** addr,
                        bool disable_shared_memory) override;

  absl::Status Reallocate(void* old_addr, size_t size,
                          void** new_addr) override;

  absl::Status Free(void* addr) override;

  absl::StatusOr<size_t> CopyFromSandbox(uintptr_t ptr,
                                         absl::Span<char> data) override;

  absl::StatusOr<size_t> CopyToSandbox(uintptr_t remote_ptr,
                                       absl::Span<const char> data) override;

  absl::Status Symbol(const char* symname, void** addr) override;

  absl::Status Exit() override;

  absl::Status SendFD(int local_fd, int* remote_fd) override;

  absl::Status RecvFD(int remote_fd, int* local_fd) override;

  absl::Status Close(int remote_fd) override;

  absl::StatusOr<std::unique_ptr<RPCChannel>> SpawnThread() override;

  absl::StatusOr<size_t> Strlen(void* str) override;

  absl::Status Call(const FuncCall& call, uint32_t tag, FuncRet* ret,
                    v::Type exp_type) override {
    *ret = FuncRet{};
    ret->ret_type = exp_type;
    ret->success = true;
    call_function_(call, ret);
    return absl::OkStatus();
  }

 private:
  CallFunctionT call_function_;
  SymbolFunctionT symbol_function_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_NULL_RPCCHANNEL_H_
