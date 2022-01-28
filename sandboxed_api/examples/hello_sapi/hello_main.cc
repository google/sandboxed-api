// Copyright 2020 Google LLC
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

// A minimal "hello world" style example of how to use Sandboxed API. This
// example does not include any error handling and will simply abort if
// something goes wrong.
// As the library function that is being called into is pure computation (i.e.
// does not issue any syscalls), the restricted default sandbox policy is being
// used.

#include <cstdlib>
#include <iostream>

// Generated header
#include "hello_sapi.sapi.h"  // NOLINT(build/include)

int main() {
  std::cout << "Calling into a sandboxee to add two numbers...\n";

  HelloSandbox sandbox;
  sandbox.Init().IgnoreError();

  HelloApi api(&sandbox);
  std::cout << "  1000 + 337 = " << api.AddTwoIntegers(1000, 337).value()
            << "\n";

  return EXIT_SUCCESS;
}
