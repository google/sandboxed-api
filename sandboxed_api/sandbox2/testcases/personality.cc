// Copyright 2019 Google LLC
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

// A binary that calls the unusual personality syscall with arguments.
// It is to test seccomp trace, notify API and checking of arguments.

#include <syscall.h>
#include <unistd.h>

#include <cstdint>

int main(int argc, char* argv[]) {
  syscall(__NR_personality, uintptr_t{1}, uintptr_t{2}, uintptr_t{3},
          uintptr_t{4}, uintptr_t{5}, uintptr_t{6});
  return 22;
}
