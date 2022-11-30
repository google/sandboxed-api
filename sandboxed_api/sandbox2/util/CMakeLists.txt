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

# sandboxed_api/sandbox2/util:bpf_helper
add_library(sandbox2_util_bpf_helper ${SAPI_LIB_TYPE}
  bpf_helper.c
  bpf_helper.h
)
add_library(sandbox2::bpf_helper ALIAS sandbox2_util_bpf_helper)
target_link_libraries(sandbox2_util_bpf_helper PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2/util:minielf
add_library(sandbox2_util_minielf ${SAPI_LIB_TYPE}
  minielf.cc
  minielf.h
)
add_library(sandbox2::minielf ALIAS sandbox2_util_minielf)
target_link_libraries(sandbox2_util_minielf PRIVATE
  absl::status
  absl::strings
  sapi::strerror
  sandbox2::util
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2/util:maps_parser
add_library(sandbox2_util_maps_parser ${SAPI_LIB_TYPE}
  maps_parser.cc
  maps_parser.h
)
add_library(sandbox2::maps_parser ALIAS sandbox2_util_maps_parser)
target_link_libraries(sandbox2_util_maps_parser
  PRIVATE absl::status
          absl::strings
          sapi::base
  PUBLIC absl::statusor
)

# sandboxed_api/sandbox2/util:syscall_trap
add_library(sandbox2_util_syscall_trap ${SAPI_LIB_TYPE}
  syscall_trap.cc
  syscall_trap.h
)
add_library(sandbox2::syscall_trap ALIAS sandbox2_util_syscall_trap)
target_link_libraries(sandbox2_util_syscall_trap
  PRIVATE absl::check
          sapi::base
          sapi::config
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  # sandboxed_api/sandbox2/util:minielf_test
  add_executable(sandbox2_minielf_test
    minielf_test.cc
  )
  set_target_properties(sandbox2_minielf_test PROPERTIES
    OUTPUT_NAME minielf_test
  )
  configure_file(testdata/hello_world
                 testdata/hello_world COPYONLY)
  configure_file(testdata/chrome_grte_header
                 testdata/chrome_grte_header COPYONLY)
  target_link_libraries(sandbox2_minielf_test
    PRIVATE absl::algorithm_container
            absl::strings
            sapi::file_helpers
            sandbox2::maps_parser
            sandbox2::minielf
            sapi::testing
            sapi::status_matchers
            sapi::test_main
    PUBLIC absl::statusor
  )
  gtest_discover_tests_xcompile(sandbox2_minielf_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2/util:maps_parser_test
  add_executable(sandbox2_maps_parser_test
    maps_parser_test.cc
  )
  set_target_properties(sandbox2_maps_parser_test PROPERTIES
    OUTPUT_NAME maps_parser_test
  )
  target_link_libraries(sandbox2_maps_parser_test PRIVATE
    sandbox2::maps_parser
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_maps_parser_test)
endif()
