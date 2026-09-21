# Copyright 2026 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_library(sapi_lwbox_runtime ${SAPI_LIB_TYPE}
  lwbox_runtime.cc
  lwbox_runtime.h
)
add_library(sapi::lwbox_runtime ALIAS sapi_lwbox_runtime)
target_link_libraries(sapi_lwbox_runtime
  PUBLIC
    absl::check
    absl::core_headers
    absl::flat_hash_set
    absl::log
    absl::node_hash_map
    absl::span
    absl::status
    absl::statusor
    absl::strings
    absl::synchronization
    sapi::sapi
    sapi::vars
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  add_executable(sapi_lwbox_runtime_test
    lwbox_runtime_test.cc
  )
  target_link_libraries(sapi_lwbox_runtime_test PRIVATE
    sapi::lwbox_runtime
    sapi::sapi
    sapi::vars
    absl::status
    absl::status_matchers
    absl::time
    absl::span
    sapi::test_main
  )
  gtest_discover_tests(sapi_lwbox_runtime_test)
endif()
