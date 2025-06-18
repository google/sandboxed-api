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
  absl::bits
  absl::status
  absl::strings
  sandbox2::elf_parser
  sapi::base
)

# sandboxed_api/sandbox2/util:elf_parser
add_library(sandbox2_util_elf_parser ${SAPI_LIB_TYPE}
  elf_parser.cc
  elf_parser.h
)
add_library(sandbox2::elf_parser ALIAS sandbox2_util_elf_parser)
target_link_libraries(sandbox2_util_elf_parser
PUBLIC absl::function_ref
       absl::status
       absl::strings
       sapi::base
PRIVATE absl::bits
        sandbox2::util
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

# sandboxed_api/sandbox2/util:pid_waiter
add_library(sandbox2_util_pid_waiter ${SAPI_LIB_TYPE}
  pid_waiter.cc
  pid_waiter.h
)
add_library(sandbox2::pid_waiter ALIAS sandbox2_util_pid_waiter)
target_link_libraries(sandbox2_util_pid_waiter
  PUBLIC absl::core_headers
         absl::cleanup
         absl::synchronization
         absl::time
         sandbox2::deadline_manager
  PRIVATE sapi::base
)


# sandboxed_api/sandbox2/util:deadline_manager
add_library(sandbox2_util_deadline_manager ${SAPI_LIB_TYPE}
  deadline_manager.cc
  deadline_manager.h
)
add_library(sandbox2::deadline_manager ALIAS sandbox2_util_deadline_manager)
target_link_libraries(sandbox2_util_deadline_manager
  PUBLIC absl::btree
         absl::core_headers
         absl::flags
         absl::function_ref
         absl::no_destructor
         absl::strings
         absl::synchronization
         absl::time
         sapi::thread
  PRIVATE absl::base
          absl::check
          absl::log
          absl::strings
          sandbox2::flags
          sandbox2::util
          sapi::base
)

# sandboxed_api/sandbox2/util:seccomp_unotify
add_library(sandbox2_util_seccomp_unotify ${SAPI_LIB_TYPE}
  seccomp_unotify.cc
  seccomp_unotify.h
)
add_library(sandbox2::seccomp_unotify ALIAS sandbox2_util_seccomp_unotify)
target_link_libraries(sandbox2_util_seccomp_unotify
  PUBLIC absl::status
         absl::statusor
         sandbox2::syscall
         sapi::fileops
  PRIVATE absl::cleanup
          absl::log
          absl::strings
          absl::synchronization
          sandbox2::bpf_helper
          sandbox2::util
          sapi::base
          sapi::strerror
          sapi::thread
)

# sandboxed_api/sandbox2/util:library_resolver
add_library(sandbox2_util_library_resolver ${SAPI_LIB_TYPE}
  library_resolver.cc
  library_resolver.h
)
add_library(sandbox2::library_resolver ALIAS sandbox2_util_library_resolver)
target_link_libraries(sandbox2_util_library_resolver
  PUBLIC absl::function_ref
         absl::statusor
         absl::strings
         sandbox2::minielf
  PRIVATE absl::base
          absl::status
          sapi::base
          sapi::config
          sapi::fileops
          sapi::file_base
          sapi::raw_logging
          sapi::status
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
            absl::status_matchers
            absl::statusor
            sapi::file_helpers
            sandbox2::maps_parser
            sandbox2::minielf
            sapi::testing
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
    absl::statusor
    sandbox2::maps_parser
    sapi::testing
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_maps_parser_test)

  # sandboxed_api/sandbox2/util:bpf_helper_test
  add_executable(sandbox2_bpf_helper_test
    bpf_helper_test.cc
  )
  set_target_properties(sandbox2_bpf_helper_test PROPERTIES
    OUTPUT_NAME bpf_helper_test
  )
  target_link_libraries(sandbox2_bpf_helper_test PRIVATE
    absl::strings
    sandbox2::bpf_helper
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_bpf_helper_test)

  # sandboxed_api/sandbox2/util:pid_waiter_test
  add_executable(sandbox2_pid_waiter_test
    pid_waiter_test.cc
  )
  set_target_properties(sandbox2_pid_waiter_test PROPERTIES
    OUTPUT_NAME pid_waiter_test
  )
  target_link_libraries(sandbox2_pid_waiter_test PRIVATE
    absl::time
    sandbox2::pid_waiter
    sapi::test_main
    sapi::thread
  )

  # sandboxed_api/sandbox2/util:deadline_manager_test
  add_executable(sandbox2_deadline_manager_test
    deadline_manager_test.cc
  )
  set_target_properties(sandbox2_deadline_manager_test PROPERTIES
    OUTPUT_NAME deadline_manager_test
  )
  target_link_libraries(sandbox2_deadline_manager_test PRIVATE
    absl::check
    absl::flags
    absl::time
    sandbox2::deadline_manager
    sandbox2::flags
    sapi::test_main
    sapi::thread
  )
  gtest_discover_tests_xcompile(sandbox2_deadline_manager_test)

  # sandboxed_api/sandbox2/util:seccomp_unotify_test
  add_executable(sandbox2_seccomp_unotify_test
    seccomp_unotify_test.cc
  )
  set_target_properties(sandbox2_seccomp_unotify_test PROPERTIES
    OUTPUT_NAME seccomp_unotify_test
  )
  target_link_libraries(sandbox2_seccomp_unotify_test PRIVATE
    sandbox2::seccomp_unotify
    absl::statusor
    sapi::fileops
    sapi::testing
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_seccomp_unotify_test)
endif()
