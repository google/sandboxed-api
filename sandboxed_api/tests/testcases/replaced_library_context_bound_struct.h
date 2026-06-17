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

#ifndef SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_CONTEXT_BOUND_STRUCT_H_
#define SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_CONTEXT_BOUND_STRUCT_H_

#include <cstddef>

// Forward declare as opaque structs to most clients.
struct ContextWithSandboxOwnedBuffer;
struct ContextWithSizedByParams;
struct ContextWithSizedAfterDecoding;

struct Dimensions {
  size_t width;
  size_t height;
};

struct Context {
  int data;
};

#endif  // SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_CONTEXT_BOUND_STRUCT_H_
