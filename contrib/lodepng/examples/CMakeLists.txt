# Copyright 2020 Google LLC
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

if(SAPI_BUILD_EXAMPLES)
  # Build the unsandboxed main
  add_executable(lodepng_unsandboxed
    main_unsandboxed.cc
    helpers.cc
  )
  target_link_libraries(lodepng_unsandboxed PRIVATE
    absl::check
    lodepng
    sapi::sapi
    sapi::temp_file
    sapi::file_base
    sapi::fileops
  )

  # Build the sandboxed main
  add_executable(lodepng_sandboxed
    main_sandboxed.cc
    sandbox.h
    helpers.cc
  )
  target_link_libraries(lodepng_sandboxed PRIVATE
    absl::check
    sapi_contrib::lodepng
    sapi::sapi
    sapi::temp_file
    sapi::fileops
    sapi::vars
    sapi::status
  )
endif()

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_executable(main_unit_test
    main_unit_test.cc
    helpers.cc
  )
  target_link_libraries(main_unit_test PRIVATE
    sapi_contrib::lodepng
    absl::check
    absl::strings
    absl::time
    sapi::sapi
    sapi::temp_file
    sapi::fileops
    sapi::status
    sapi::test_main
    sapi::vars
  )
  gtest_discover_tests(main_unit_test)
endif()
