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

#ifndef SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_PRIVATE_H_
#define SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_PRIVATE_H_

#include <cstddef>

#include "sandboxed_api/tests/testcases/replaced_library_context_bound_struct.h"

// Private structs, exposed in a private header for this test.
// In a real scenario, these could be defined in .c files
// TODO(b/491828958): we will need to see how to handle the .c file case.

struct ContextWithSandboxOwnedBuffer {
  struct Context base;
  char buff_inline[20];
  char* buff_null_terminated;
};

struct ContextWithSizedByParams {
  Dimensions param_sizes;
  char* sized_by_params;
};

struct ContextWithSizedAfterDecoding {
  Dimensions decoded_sizes;
  char* char_buff;
  unsigned int* unsigned_int_buff;
};

struct ContextStoresPartOfInputSize {
  size_t num_channels;
};

#endif  // SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_PRIVATE_H_
