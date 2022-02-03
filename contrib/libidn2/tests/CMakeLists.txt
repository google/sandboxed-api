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

add_executable(libidn2_sapi_test
  libidn2_sapi_test.cc
)
target_link_libraries(libidn2_sapi_test PRIVATE
  sapi_contrib::libidn2
  sapi::test_main
)
gtest_discover_tests(libidn2_sapi_test)
