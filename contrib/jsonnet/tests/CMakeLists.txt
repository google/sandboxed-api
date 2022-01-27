# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# We need to prepare convenient directories so the tests will be able to access them
file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/tests/tests_input)
file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/tests/tests_output)
file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/tests/tests_expected_output)

add_custom_target(test_preparation ALL
  COMMAND cp ${PROJECT_SOURCE_DIR}/examples/jsonnet_codes/* ${PROJECT_BINARY_DIR}/tests/tests_input
  COMMAND cp ${PROJECT_SOURCE_DIR}/examples/jsonnet_codes_expected_output/* ${PROJECT_BINARY_DIR}/tests/tests_expected_output
)

add_executable(tests
  jsonnet_test_helper.cc
  jsonnet_test_helper.h
  jsonnet_tests.cc
)

target_include_directories(tests PUBLIC
  ${PROJECT_SOURCE_DIR}/headers
)

target_link_libraries(tests
  jsonnet_sapi sapi::sapi
  gtest gmock gtest_main
)

gtest_discover_tests(tests)
