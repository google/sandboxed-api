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

add_executable(tests
  check_tag.h
  check_tag.cc
  defer_strile_writing.cc
  long_tag.cc
  raw_decode.cc
  short_tag.cc
  helper.h
  helper.cc
)
target_link_libraries(tests
  PRIVATE sapi::base
          sapi::sapi
          sapi::temp_file
          sapi_contrib::libtiff
          gmock
          gtest_main
)

gtest_discover_tests(tests PROPERTIES
  ENVIRONMENT "TEST_SRCDIR=${PROJECT_SOURCE_DIR}"
)
