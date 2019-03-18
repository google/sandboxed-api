// Copyright 2019 Google LLC. All Rights Reserved.
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

#ifndef SANDBOXED_API_UTIL_FLAG_H_
#define SANDBOXED_API_UTIL_FLAG_H_
#include <gflags/gflags.h>

#define ABSL_FLAG(type, name, default_value, help) \
  DEFINE_##type(name, default_value, help)
#define ABSL_RETIRED_FLAG ABSL_FLAG
#define ABSL_DECLARE_FLAG(type, name) DECLARE_##type(name)

// Internal defines for compatility with gflags and standard integer types.
#define DECLARE_int32_t DECLARE_int32
#define DECLARE_int64_t DECLARE_int64
#define DECLARE_uint32_t DECLARE_uint32
#define DECLARE_uint64_t DECLARE_uint64
#define DEFINE_int32_t DEFINE_int32
#define DEFINE_int64_t DEFINE_int64
#define DEFINE_uint32_t DEFINE_uint32
#define DEFINE_uint64_t DEFINE_uint64

namespace absl {

template <typename T>
const T& GetFlag(const T& flag) {
  return flag;
}

template <typename T>
void SetFlag(T* flag, const T& value) {
  *flag = value;
}

}  // namespace absl

#endif  // SANDBOXED_API_UTIL_FLAG_H_
