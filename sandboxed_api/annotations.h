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

// Test library for sapi_replacement_library rule.
// It's supposed to include all patterns that we support for transparent
// sandboxing. The test for the library runs with normal and sandboxed
// replacement library.

#ifndef SANDBOXED_API_ANNOTATIONS_H_
#define SANDBOXED_API_ANNOTATIONS_H_

#include "sandboxed_api/annotations_internal.h"

// NOTE: this functionality is experimental and may change in the future.

// Lists function that are to be sandboxed, or not sandboxed, correspondingly.
//
// For example:
//   SANDBOX_FUNCS(foo_init, foo_destroy, foo_something);
// or:
//   SANDBOX_IGNORE_FUNCS(foo_unused_func, foo_unsupported_signature);
//
// Functions that are not selected by these macros will not be available
// in the sandbox. If none of these macros are used, all functions in the
// library header files are selected for sandboxing. But if used, only one
// of the macros can be used in a given file once.
#define SANDBOX_FUNCS(...) SANDBOX_FUNCS_IMPL(__VA_ARGS__)
#define SANDBOX_IGNORE_FUNCS(...) SANDBOX_IGNORE_FUNCS_IMPL(__VA_ARGS__)

#endif  // SANDBOXED_API_ANNOTATIONS_H_
