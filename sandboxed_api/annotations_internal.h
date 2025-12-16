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

#ifndef SANDBOXED_API_ANNOTATIONS_INTERNAL_H_
#define SANDBOXED_API_ANNOTATIONS_INTERNAL_H_

#define SANDBOX_INTERNAL_PARENS ()
#define SANDBOX_INTERNAL_STRINGIFY(arg) #arg
#define SANDBOX_INTERNAL_CAT2(a, b) a##b
#define SANDBOX_INTERNAL_CAT(a, b) SANDBOX_INTERNAL_CAT2(a, b)

#define SANDBOX_INTERNAL_EXPAND(...)                 \
  SANDBOX_INTERNAL_EXPAND4(SANDBOX_INTERNAL_EXPAND4( \
      SANDBOX_INTERNAL_EXPAND4(SANDBOX_INTERNAL_EXPAND4(__VA_ARGS__))))
#define SANDBOX_INTERNAL_EXPAND4(...)                \
  SANDBOX_INTERNAL_EXPAND3(SANDBOX_INTERNAL_EXPAND3( \
      SANDBOX_INTERNAL_EXPAND3(SANDBOX_INTERNAL_EXPAND3(__VA_ARGS__))))
#define SANDBOX_INTERNAL_EXPAND3(...)                \
  SANDBOX_INTERNAL_EXPAND2(SANDBOX_INTERNAL_EXPAND2( \
      SANDBOX_INTERNAL_EXPAND2(SANDBOX_INTERNAL_EXPAND2(__VA_ARGS__))))
#define SANDBOX_INTERNAL_EXPAND2(...)                \
  SANDBOX_INTERNAL_EXPAND1(SANDBOX_INTERNAL_EXPAND1( \
      SANDBOX_INTERNAL_EXPAND1(SANDBOX_INTERNAL_EXPAND1(__VA_ARGS__))))
#define SANDBOX_INTERNAL_EXPAND1(...) __VA_ARGS__

#define SANDBOX_INTERNAL_FOREACH(macro, ...) \
  __VA_OPT__(SANDBOX_INTERNAL_EXPAND(        \
      SANDBOX_INTERNAL_FOREACH_HELPER(macro, __VA_ARGS__)))

#define SANDBOX_INTERNAL_FOREACH_HELPER(macro, arg, ...)                 \
  macro(arg),                                                            \
      __VA_OPT__(SANDBOX_INTERNAL_FOREACH_AGAIN SANDBOX_INTERNAL_PARENS( \
          macro, __VA_ARGS__))

#define SANDBOX_INTERNAL_FOREACH_AGAIN() SANDBOX_INTERNAL_FOREACH_HELPER

#define SANDBOX_FUNCS_IMPL(...)                                    \
  const char* SANDBOX_INTERNAL_CAT(sandbox_funcs_, __LINE__)[] = { \
      SANDBOX_INTERNAL_FOREACH(SANDBOX_INTERNAL_STRINGIFY, ##__VA_ARGS__)}

#define SANDBOX_IGNORE_FUNCS_IMPL(...)                                    \
  const char* SANDBOX_INTERNAL_CAT(sandbox_ignore_funcs_, __LINE__)[] = { \
      SANDBOX_INTERNAL_FOREACH(SANDBOX_INTERNAL_STRINGIFY, ##__VA_ARGS__)}

#endif  // SANDBOXED_API_ANNOTATIONS_INTERNAL_H_
