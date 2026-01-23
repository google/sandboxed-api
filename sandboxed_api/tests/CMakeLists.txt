# Copyright 2025 Google LLC
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

if(BUILD_TESTING AND SAPI_BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  # sandboxed_api/examples/sum/lib:sum
  add_library(sapi_test_lib STATIC
    sapi_test_lib_cpp.cc
  )
  add_library(sapi::test_lib ALIAS sapi_test_lib)
  target_link_libraries(sapi_test_lib PRIVATE
    absl::algorithm_container
    absl::span
    sapi::base
  )

  # sandboxed_api/examples/sum/lib:sum-sapi
  add_sapi_library(sapi_test-sapi
    FUNCTIONS accumulate
    INPUTS sapi_test_lib_cpp.cc
    LIBRARY sapi_test_lib
    LIBRARY_NAME SapiTest
    NAMESPACE ""
  )
  add_library(sapi::sapi_test_sapi ALIAS sapi_test-sapi)
  target_link_libraries(sapi_test-sapi PRIVATE
    sapi::base
  )

  # sandboxed_api:sapi_test
  add_executable(sapi_test
    sapi_test.cc
  )
  target_link_libraries(sapi_test PRIVATE
    absl::status
    absl::status_matchers
    absl::statusor
    absl::time
    benchmark
    sandbox2::result
    sapi::fileops
    sapi::proto_arg_proto
    sapi::sapi
    sapi::sapi_test_sapi
    sapi::status
    sapi::stringop_sapi
    sapi::sum_sapi
    sapi::test_main
    sapi::testing
    sapi::thread
  )
  gtest_discover_tests_xcompile(sapi_test)
endif()
