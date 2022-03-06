# Copyright 2022 Google LLC
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

include(GoogleTest)

add_executable(sapi_brotli_test
  brotli_test.cc
  ../utils/utils_brotli.cc
  ../utils/utils_brotli_enc.cc
  ../utils/utils_brotli_dec.cc
)
target_link_libraries(sapi_brotli_test PRIVATE
  sapi_contrib::brotli
  sapi::test_main
)
gtest_discover_tests(sapi_brotli_test PROPERTIES
  ENVIRONMENT "TEST_FILES_DIR=${PROJECT_SOURCE_DIR}/files"
)
