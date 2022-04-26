# Copyright 2019 Google LLC
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

# sandboxed_api/tools/filewrapper:filewrapper
add_executable(filewrapper
  filewrapper.cc
)
target_link_libraries(filewrapper PRIVATE
  absl::strings
  absl::str_format
  sapi::fileops
  sapi::strerror
  sapi::base
  sapi::raw_logging
)

sapi_cc_embed_data(NAME filewrapper_embedded
  NAMESPACE ""
  SOURCES testdata/filewrapper_embedded.bin
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  # sandboxed_api/tools/filewrapper:filewrapper_test
  add_executable(sapi_filewrapper_test
    filewrapper_test.cc
  )
  set_target_properties(sapi_filewrapper_test PROPERTIES
    OUTPUT_NAME filewrapper_test
  )
  configure_file(testdata/filewrapper_embedded.bin
                 testdata/filewrapper_embedded.bin COPYONLY)
  target_link_libraries(sapi_filewrapper_test PRIVATE
    absl::strings
    filewrapper_embedded
    sapi::file_helpers
    sapi::fileops
    sapi::testing
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sapi_filewrapper_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )
endif()
