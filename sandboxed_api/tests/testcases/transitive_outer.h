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

#ifndef SANDBOXED_API_TESTS_TESTCASES_TRANSITIVE_OUTER_H_
#define SANDBOXED_API_TESTS_TESTCASES_TRANSITIVE_OUTER_H_

extern "C" {
int transitive_outer_add(int x, int y);
int transitive_outer_pid();
int transitive_outer_inner_pid();
bool transitive_outer_is_sandboxed();
bool transitive_outer_inner_is_sandboxed();
}

#endif  // SANDBOXED_API_TESTS_TESTCASES_TRANSITIVE_OUTER_H_
