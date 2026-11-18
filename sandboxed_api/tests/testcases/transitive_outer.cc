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

#include "sandboxed_api/tests/testcases/transitive_outer.h"

#include <errno.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "sandboxed_api/tests/testcases/transitive_inner.h"

extern "C" {

int transitive_outer_add(int x, int y) {
  return transitive_inner_add(x, y) + 100;
}

int transitive_outer_pid() { return getpid(); }

int transitive_outer_inner_pid() { return transitive_inner_pid(); }

bool transitive_outer_is_sandboxed() {
  return syscall(0x2f000fdb) == -1 && errno == 0xfdb;
}

bool transitive_outer_inner_is_sandboxed() {
  return transitive_inner_is_sandboxed();
}

}  // extern "C"
