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

#include <stdint.h>

#include "sandboxed_api/annotations.h"

// None of these signatures needs an annotation on its own account: every
// argument is a scalar, and `apply_twice`'s callback neither takes nor returns
// a pointer. The callback typedef below is there for the generated machinery it
// reaches -- using a callback typedef is what makes the generator emit that
// typedef's sandboxee invoker and its pool of host thunks.

extern "C" {

// What `apply_twice` calls. Nothing in it needs describing beyond its
// existence, which is what makes it the smallest case the reverse direction
// can be built against.
typedef int (*adder)(int a, int b);

uintptr_t get_adder(int which);

int apply_twice(adder cb, int a, int b);

}  // extern "C"
