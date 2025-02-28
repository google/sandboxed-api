// Copyright 2025 Google LLC
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

// This header provides a C-wrapper for sandbox2::util functions that may be
// useful in C hostcode.

#ifndef SANDBOXED_API_SANDBOX2_UTIL_C_H_
#define SANDBOXED_API_SANDBOX2_UTIL_C_H_

#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Returns true if the current process is running inside Sandbox2.
bool IsRunningInSandbox2();

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // SANDBOXED_API_SANDBOX2_UTIL_C_H_
