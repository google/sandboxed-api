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

#ifndef SANDBOXED_API_TESTING_H_
#define SANDBOXED_API_TESTING_H_

#include <cstdlib>
#include <string>

#include "absl/strings/string_view.h"

// The macro SKIP_ANDROID can be used in tests to skip running a
// given test (by emitting 'retrun') when running on Android. Example:
//
//    TEST(Foo, Bar) {
//      SKIP_ANDROID;
//      [...]
//      }
//
// The reason for this is because certain unit tests require the use of user
// namespaces which are not present on Android.
#if defined(__ANDROID__)
#define SKIP_ANDROID return
#else
#define SKIP_ANDROID
#endif

// The macro SKIP_SANITIZERS_AND_COVERAGE can be used in tests to skip running
// a given test (by emitting 'return') when running under one of the sanitizers
// (ASan, MSan, TSan) or under code coverage. Example:
//
//   TEST(Foo, Bar) {
//     SKIP_SANITIZERS_AND_COVERAGE;
//     [...]
//   }
//
// The reason for this is because Bazel options are inherited to binaries in
// data dependencies and cannot be per-target, which means when running a test
// with a sanitizer or coverage, the sandboxee as data dependency will also be
// compiled with sanitizer or coverage, which creates a lot of side effects and
// violates the sandbox policy prepared for the test.
// See b/7981124. // google3-only(internal reference)
// In other words, those tests cannot work under sanitizers or coverage, so we
// skip them in such situation using this macro.
//
// The downside of this approach is that no coverage will be collected.
// To still have coverage, pre-compile sandboxees and add them as test data,
// then there will be no need to skip tests.
#if defined(ABSL_HAVE_ADDRESS_SANITIZER) || \
    defined(ABSL_HAVE_MEMORY_SANITIZER) || defined(ABSL_HAVE_THREAD_SANITIZER)
#define SKIP_SANITIZERS_AND_COVERAGE return
#else
#define SAPI_BUILDDATA_COVERAGE_ENABLED false
#define SKIP_SANITIZERS_AND_COVERAGE                                        \
  do {                                                                      \
    if (SAPI_BUILDDATA_COVERAGE_ENABLED || getenv("COVERAGE") != nullptr) { \
      return;                                                               \
    }                                                                       \
  } while (0)
#endif

namespace sapi {

// Returns a writable path usable in tests. If the name argument is specified,
// returns a name under that path. This can then be used for creating temporary
// test files and/or directories.
std::string GetTestTempPath(absl::string_view name = {});

// Returns a filename relative to the sandboxed_api directory at the root of the
// source tree. Use this to access data files in tests.
std::string GetTestSourcePath(absl::string_view name);

}  // namespace sapi

#endif  // SANDBOXED_API_TESTING_H_
