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

#ifndef SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_REVERSE_CALLBACKS_H_
#define SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_REVERSE_CALLBACKS_H_

#include <stdint.h>

// A library used to test callbacks in both directions:
// - Forward callbacks implemented by the host and invoked or retained by the
//   sandboxed library.
// - Reverse callbacks implemented by the sandboxed library and returned to the
//   host or stored in struct fields.

extern "C" {

// Hands back the address of one of this library's own functions. 0 for an
// index nobody implements.
//
// A raw address is not a shape any real library should have, and not the one
// the reverse direction will be built around. This exists so that the tests
// for the thunk pool can drive its binding entry point directly, which needs
// a sandboxee address that has not already been turned into a host-callable
// one.
uintptr_t get_adder(int which);

// The forward direction, and the vehicle for the interesting case: calls
// `cb(cb(a, b), b)`, so the callback is genuinely reentered.
int apply_twice(int (*cb)(int, int), int a, int b);

}  // extern "C"

#endif  // SANDBOXED_API_TESTS_TESTCASES_REPLACED_LIBRARY_REVERSE_CALLBACKS_H_
