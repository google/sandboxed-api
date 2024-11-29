// Copyright 2024 Google LLC
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

#ifndef SANDBOXED_API_UTIL_THREAD_H_
#define SANDBOXED_API_UTIL_THREAD_H_

#include <memory>
#include <utility>
#include <thread>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"

namespace sapi {

class Thread {
 public:
  static void StartDetachedThread(absl::AnyInvocable<void() &&> functor,
                                  absl::string_view name_prefix = "") {
    std::thread(std::move(functor)).detach();
  }

  Thread() = default;

  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;

  Thread(Thread&&) = default;
  Thread& operator=(Thread&&) = default;

  Thread(absl::AnyInvocable<void() &&> functor,
         absl::string_view name_prefix = "") {
    thread_ = std::thread(std::move(functor));
  }

  template <class CL>
  Thread(CL* ptr, void (CL::*ptr_to_member)(),
         absl::string_view name_prefix = "") {
    thread_ = std::thread(ptr_to_member, ptr);
  }

  pthread_t handle() {
    return thread_.native_handle();
  }

  void Join() {
    thread_.join();
  }

  bool IsJoinable() {
    return thread_.joinable();
  }

 private:
  std::thread thread_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_UTIL_THREAD_H_
