# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# sandboxed_api/sandbox2/util:bpf_helper
add_library(sandbox2_util_bpf_helper STATIC
  bpf_helper.c
  bpf_helper.h
)
add_library(sandbox2::bpf_helper ALIAS sandbox2_util_bpf_helper)
target_link_libraries(sandbox2_util_bpf_helper PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2/util:file_helpers
add_library(sandbox2_util_file_helpers STATIC
  file_helpers.cc
  file_helpers.h
)
add_library(sandbox2::file_helpers ALIAS sandbox2_util_file_helpers)
target_link_libraries(sandbox2_util_file_helpers PRIVATE
  absl::status
  absl::strings
  sapi::base
)

# sandboxed_api/sandbox2/util:fileops
add_library(sandbox2_util_fileops STATIC
  fileops.cc
  fileops.h
)
add_library(sandbox2::fileops ALIAS sandbox2_util_fileops)
target_link_libraries(sandbox2_util_fileops PRIVATE
  absl::strings
  sandbox2::strerror
  sapi::base
)

# sandboxed_api/sandbox2/util:file_base
add_library(sandbox2_util_file_base STATIC
  path.cc
  path.h
)
add_library(sandbox2::file_base ALIAS sandbox2_util_file_base)
target_link_libraries(sandbox2_util_file_base PRIVATE
  absl::strings
  sapi::base
)

# sandboxed_api/sandbox2/util:strerror
add_library(sandbox2_util_strerror STATIC
  strerror.cc
  strerror.h
)
add_library(sandbox2::strerror ALIAS sandbox2_util_strerror)
target_link_libraries(sandbox2_util_strerror PRIVATE
  absl::strings
  sapi::base
)

# sandboxed_api/sandbox2/util:minielf
add_library(sandbox2_util_minielf STATIC
  minielf.cc
  minielf.h
)
add_library(sandbox2::minielf ALIAS sandbox2_util_minielf)
target_link_libraries(sandbox2_util_minielf PRIVATE
  absl::status
  absl::strings
  sandbox2::util
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2/util:temp_file
add_library(sandbox2_util_temp_file STATIC
  temp_file.cc
  temp_file.h
)
add_library(sandbox2::temp_file ALIAS sandbox2_util_temp_file)
target_link_libraries(sandbox2_util_temp_file
  PRIVATE absl::strings
          sandbox2::fileops
          sandbox2::strerror
          sapi::base
  PUBLIC absl::status
         absl::statusor
)

# sandboxed_api/sandbox2/util:maps_parser
add_library(sandbox2_util_maps_parser STATIC
  maps_parser.cc
  maps_parser.h
)
add_library(sandbox2::maps_parser ALIAS sandbox2_util_maps_parser)
target_link_libraries(sandbox2_util_maps_parser PRIVATE
  absl::status
  absl::statusor
  absl::strings
  sapi::base
)

# sandboxed_api/sandbox2/util:runfiles
add_library(sandbox2_util_runfiles STATIC
  runfiles.h
  runfiles_nobazel.cc
)
add_library(sandbox2::runfiles ALIAS sandbox2_util_runfiles)
target_link_libraries(sandbox2_util_runfiles PRIVATE
  absl::str_format
  absl::strings
  sandbox2::file_base
  sapi::base
  sapi::flags
  sapi::raw_logging
)

if(SAPI_ENABLE_TESTS)
  # sandboxed_api/sandbox2/util:file_helpers_test
  add_executable(file_helpers_test
    file_helpers_test.cc
  )
  target_link_libraries(file_helpers_test PRIVATE
    absl::strings
    sandbox2::file_helpers
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests(file_helpers_test)

  # sandboxed_api/sandbox2/util:fileops_test
  add_executable(fileops_test
    fileops_test.cc
  )
  target_link_libraries(fileops_test PRIVATE
    absl::strings
    sandbox2::file_helpers
    sandbox2::fileops
    sandbox2::testing
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests(fileops_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2/util:file_base_test
  add_executable(file_base_test
    path_test.cc
  )
  target_link_libraries(file_base_test PRIVATE
    absl::strings
    sandbox2::file_base
    sapi::test_main
  )
  gtest_discover_tests(file_base_test)

  # sandboxed_api/sandbox2/util:strerror
  add_executable(strerror_test
    strerror_test.cc
  )
  target_link_libraries(strerror_test PRIVATE
    absl::strings
    sandbox2::strerror
    sapi::test_main
  )
  gtest_discover_tests(strerror_test)

  # sandboxed_api/sandbox2/util:minielf_test
  add_executable(minielf_test
    minielf_test.cc
  )
  configure_file(testdata/hello_world
                 testdata/hello_world COPYONLY)
  configure_file(testdata/chrome_grte_header
                 testdata/chrome_grte_header COPYONLY)
  target_link_libraries(minielf_test PRIVATE
    absl::strings
    sandbox2::maps_parser
    sandbox2::minielf
    sandbox2::testing
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests(minielf_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2/util:temp_file_test
  add_executable(temp_file_test
    temp_file_test.cc
  )
  target_link_libraries(temp_file_test PRIVATE
    sandbox2::file_base
    sandbox2::fileops
    sandbox2::temp_file
    sandbox2::testing
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests(temp_file_test)

  # sandboxed_api/sandbox2/util:maps_parser_test
  add_executable(maps_parser_test
    maps_parser_test.cc
  )
  target_link_libraries(maps_parser_test PRIVATE
    sandbox2::maps_parser
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests(maps_parser_test)
endif()
