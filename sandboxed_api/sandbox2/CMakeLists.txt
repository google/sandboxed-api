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

add_subdirectory(examples)
add_subdirectory(unwind)
add_subdirectory(util)
add_subdirectory(network_proxy)

# sandboxed_api/sandbox2:bpfdisassembler
add_library(sandbox2_bpfdisassembler ${SAPI_LIB_TYPE}
  bpfdisassembler.cc
  bpfdisassembler.h
)
add_library(sandbox2::bpfdisassembler ALIAS sandbox2_bpfdisassembler)
target_link_libraries(sandbox2_bpfdisassembler PRIVATE
  absl::span
  absl::strings
  sapi::base
)

# sandboxed_api/sandbox2:regs
add_library(sandbox2_regs ${SAPI_LIB_TYPE}
  regs.cc
  regs.h
)
add_library(sandbox2::regs ALIAS sandbox2_regs)
target_link_libraries(sandbox2_regs
  PUBLIC absl::status
         sapi::config
         sandbox2::syscall
         sandbox2::violation_proto
  PRIVATE absl::core_headers
          absl::strings
          sapi::strerror
          sapi::base
          sapi::status
)

# sandboxed_api/sandbox2:syscall
add_library(sandbox2_syscall ${SAPI_LIB_TYPE}
  syscall.cc
  syscall.h
  syscall_defs.cc
  syscall_defs.h
)
add_library(sandbox2::syscall ALIAS sandbox2_syscall)
target_link_libraries(sandbox2_syscall
  PRIVATE absl::algorithm_container
          absl::span
          absl::str_format
          absl::strings
          sandbox2::util
          sapi::base
  PUBLIC absl::log
)

# sandboxed_api/sandbox2:result
add_library(sandbox2_result ${SAPI_LIB_TYPE}
  result.cc
  result.h
)
add_library(sandbox2::result ALIAS sandbox2_result)
target_link_libraries(sandbox2_result PRIVATE
  absl::base
  absl::strings
  sapi::config
  sandbox2::regs
  sandbox2::syscall
  sandbox2::util
  sapi::base
  sapi::status
)

# sandboxed_api/sandbox2:logserver_proto
sapi_protobuf_generate_cpp(_sandbox2_logserver_pb_h _sandbox2_logserver_pb_cc
  logserver.proto
)
add_library(sandbox2_logserver_proto ${SAPI_LIB_TYPE}
  ${_sandbox2_logserver_pb_cc}
  ${_sandbox2_logserver_pb_h}
)
add_library(sandbox2::logserver_proto ALIAS sandbox2_logserver_proto)
target_link_libraries(sandbox2_logserver_proto
  PRIVATE sapi::base
  PUBLIC protobuf::libprotobuf
)

# sandboxed_api/sandbox2:logserver
add_library(sandbox2_logserver ${SAPI_LIB_TYPE}
  logserver.cc
  logserver.h
)
add_library(sandbox2::logserver ALIAS sandbox2_logserver)
target_link_libraries(sandbox2_logserver
  PRIVATE sandbox2::comms
          sandbox2::logserver_proto
          sapi::base
  PUBLIC absl::log
)

# sandboxed_api/sandbox2:logsink
add_library(sandbox2_logsink ${SAPI_LIB_TYPE}
  logsink.cc
  logsink.h
)
add_library(sandbox2::logsink ALIAS sandbox2_logsink)
target_link_libraries(sandbox2_logsink
  PRIVATE absl::strings
          sandbox2::comms
          sandbox2::logserver_proto
          sapi::base
  PUBLIC absl::synchronization
         absl::log
)

# sandboxed_api/sandbox2:ipc
add_library(sandbox2_ipc ${SAPI_LIB_TYPE}
  ipc.cc
  ipc.h
)
add_library(sandbox2::ipc ALIAS sandbox2_ipc)
target_link_libraries(sandbox2_ipc PRIVATE
  absl::core_headers
  absl::strings
  sandbox2::comms
  sandbox2::logserver
  sandbox2::logsink
  sandbox2::network_proxy_client
  sandbox2::network_proxy_server
  sapi::base
)

# sandboxed_api/sandbox2:policy
add_library(sandbox2_policy ${SAPI_LIB_TYPE}
  policy.cc
  policy.h
)
add_library(sandbox2::policy ALIAS sandbox2_policy)
target_link_libraries(sandbox2_policy PRIVATE
  absl::core_headers
  absl::optional
  sandbox2::bpf_helper
  sandbox2::bpfdisassembler
  sandbox2::comms
  sandbox2::namespace
  sandbox2::regs
  sandbox2::syscall
  sandbox2::violation_proto
  sapi::base
  sapi::config
)

# sandboxed_api/sandbox2:notify
add_library(sandbox2_notify ${SAPI_LIB_TYPE}
  notify.h
)
add_library(sandbox2::notify ALIAS sandbox2_notify)
target_link_libraries(sandbox2_notify
  PUBLIC absl::core_headers
         absl::log
         sandbox2::comms
         sandbox2::result
         sandbox2::syscall
         sandbox2::util
  PRIVATE sapi::base
          sapi::config
)

# sandboxed_api/sandbox2:limits
add_library(sandbox2_limits ${SAPI_LIB_TYPE}
  limits.h
)
add_library(sandbox2::limits ALIAS sandbox2_limits)
target_link_libraries(sandbox2_limits PRIVATE
  absl::core_headers
  absl::time
  sapi::base
)

# sandboxed_api/sandbox2:forkserver_bin
add_executable(sandbox2_forkserver_bin
  forkserver_bin.cc
)
set_target_properties(sandbox2_forkserver_bin PROPERTIES
    OUTPUT_NAME forkserver_bin)
add_executable(sandbox2::forkserver_bin ALIAS sandbox2_forkserver_bin)
target_link_libraries(sandbox2_forkserver_bin PRIVATE
  absl::log_globals
  sandbox2::client
  sandbox2::comms
  sandbox2::forkserver
  sandbox2::sanitizer
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2:forkserver_bin_embed
sapi_cc_embed_data(NAME sandbox2_forkserver_bin_embed
  OUTPUT_NAME forkserver_bin_embed
  NAMESPACE ""
  SOURCES sandbox2::forkserver_bin
)
add_library(sandbox2::forkserver_bin_embed ALIAS sandbox2_forkserver_bin_embed)

# sandboxed_api/sandbox2:global_forkserver
add_library(sandbox2_global_forkserver ${SAPI_LIB_TYPE}
  global_forkclient.cc
  global_forkclient.h
)
add_library(sandbox2::global_forkserver ALIAS sandbox2_global_forkserver)
target_link_libraries(sandbox2_global_forkserver
  PRIVATE absl::strings
          absl::status
          absl::statusor
          absl::log
          sandbox2::client
          sandbox2::forkserver_bin_embed
          sapi::strerror
          sandbox2::util
          sapi::base
          sapi::config
          sapi::embed_file
          sapi::fileops
          sapi::raw_logging
          sapi::status
  PUBLIC absl::core_headers
         absl::flags
         absl::synchronization
         sandbox2::comms
         sandbox2::fork_client
         sandbox2::forkserver_proto
)

# sandboxed_api/sandbox2:start_global_forkserver_lib_constructor
# Use only if Sandbox2 global forkserver has to be started very early on.
# By default the forkserver is started on demand.
add_library(sandbox2_start_global_forkserver_lib_constructor STATIC
  global_forkclient_lib_ctor.cc
)
add_library(sandbox2::start_global_forkserver_lib_constructor ALIAS
  sandbox2_start_global_forkserver_lib_constructor)
target_link_libraries(sandbox2_start_global_forkserver_lib_constructor PRIVATE
  absl::core_headers
  sapi::base
  sandbox2::fork_client
  sandbox2::global_forkserver
)

# sandboxed_api/sandbox2:executor
add_library(sandbox2_executor ${SAPI_LIB_TYPE}
  executor.cc
  executor.h
)
add_library(sandbox2::executor ALIAS sandbox2_executor)
target_link_libraries(sandbox2_executor
  PRIVATE absl::core_headers
          sandbox2::forkserver_proto
          sandbox2::ipc
          sandbox2::limits
          sandbox2::namespace
          sandbox2::util
          sapi::base
          sapi::status_proto
  PUBLIC absl::log
         absl::span
         absl::status
         absl::strings
         sapi::config
         sapi::fileops
         sapi::status
         sandbox2::fork_client
         sandbox2::global_forkserver
)

# sandboxed_api/sandbox2:sandbox2
add_library(sandbox2_sandbox2 ${SAPI_LIB_TYPE}
  monitor.cc
  monitor.h
  policybuilder.cc
  policybuilder.h
  sandbox2.cc
  sandbox2.h
  stack_trace.cc
  stack_trace.h
)
add_library(sandbox2::sandbox2 ALIAS sandbox2_sandbox2)
target_link_libraries(sandbox2_sandbox2
  PRIVATE absl::core_headers
          absl::cleanup
          absl::flat_hash_set
          absl::optional
          absl::span
          absl::str_format
          absl::strings
          sapi::strerror
          sapi::base
  PUBLIC  absl::flat_hash_map
          absl::status
          absl::statusor
          absl::synchronization
          absl::time
          sapi::config
          sapi::file_base
          sapi::fileops
          sapi::status
          sapi::temp_file
          sandbox2::bpf_helper
          sandbox2::client
          sandbox2::comms
          sandbox2::executor
          sandbox2::fork_client
          sandbox2::forkserver_proto
          sandbox2::global_forkserver
          sandbox2::ipc
          sandbox2::limits
          sandbox2::logsink
          sandbox2::mounts
          sandbox2::mount_tree_proto
          sandbox2::namespace
          sandbox2::network_proxy_client
          sandbox2::network_proxy_server
          sandbox2::notify
          sandbox2::policy
          sandbox2::regs
          sandbox2::result
          sandbox2::sanitizer
          sandbox2::syscall
          sandbox2::unwind
          sandbox2::unwind_proto
          sandbox2::util
          sandbox2::violation_proto
)

# sandboxed_api/sandbox2:client
add_library(sandbox2_client ${SAPI_LIB_TYPE}
  client.cc
  client.h
)
add_library(sandbox2::client ALIAS sandbox2_client)
target_link_libraries(sandbox2_client
  PRIVATE absl::core_headers
          absl::strings
          sandbox2::sanitizer
          sapi::strerror
          sapi::base
          sapi::raw_logging
  PUBLIC absl::flat_hash_map
         sandbox2::comms
         sandbox2::logsink
         sandbox2::network_proxy_client
)

# sandboxed_api/sandbox2:sanitizer
add_library(sandbox2_sanitizer ${SAPI_LIB_TYPE}
  sanitizer.cc
  sanitizer.h
)
add_library(sandbox2::sanitizer ALIAS sandbox2_sanitizer)
target_link_libraries(sandbox2_sanitizer
  PRIVATE absl::strings
          sandbox2::util
          sapi::file_helpers
          sapi::fileops
          sapi::strerror
          sapi::raw_logging
          sapi::base
  PUBLIC absl::core_headers
         absl::flat_hash_set
         absl::status
         absl::statusor
)

# sandboxed_api/sandbox2:forkserver
add_library(sandbox2_forkserver ${SAPI_LIB_TYPE}
  forkserver.cc
  forkserver.h
)
add_library(sandbox2::forkserver ALIAS sandbox2_forkserver)
target_link_libraries(sandbox2_forkserver PRIVATE
  absl::flat_hash_map
  absl::flat_hash_set
  absl::status
  absl::statusor
  absl::str_format
  absl::strings
  libcap::libcap
  sandbox2::bpf_helper
  sandbox2::client
  sandbox2::comms
  sapi::fileops
  sandbox2::fork_client
  sandbox2::forkserver_proto
  sandbox2::namespace
  sandbox2::policy
  sapi::strerror
  sandbox2::sanitizer
  sandbox2::syscall
  sandbox2::unwind
  sandbox2::util
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2:fork_client
add_library(sandbox2_fork_client ${SAPI_LIB_TYPE}
        fork_client.cc
        fork_client.h
)
add_library(sandbox2::fork_client ALIAS sandbox2_fork_client)
target_link_libraries(sandbox2_fork_client PRIVATE
  absl::core_headers
  absl::synchronization
  sandbox2::comms
  sandbox2::forkserver_proto
  sapi::base
)

# sandboxed_api/sandbox2:mounts
add_library(sandbox2_mounts ${SAPI_LIB_TYPE}
  mounts.cc
  mounts.h
)
add_library(sandbox2::mounts ALIAS sandbox2_mounts)
target_link_libraries(sandbox2_mounts
  PRIVATE absl::flat_hash_set
          absl::str_format
          protobuf::libprotobuf
          sapi::config
          sapi::file_base
          sapi::fileops
          sandbox2::minielf
          sapi::strerror
          sapi::base
          sapi::raw_logging
          sapi::status
  PUBLIC absl::core_headers
         absl::status
         absl::statusor
         absl::strings
         sandbox2::mount_tree_proto
)

# sandboxed_api/sandbox2:namespace
add_library(sandbox2_namespace ${SAPI_LIB_TYPE}
  namespace.cc
  namespace.h
)
add_library(sandbox2::namespace ALIAS sandbox2_namespace)
target_link_libraries(sandbox2_namespace PRIVATE
  absl::core_headers
  absl::str_format
  absl::strings
  protobuf::libprotobuf
  sapi::file_base
  sapi::fileops
  sandbox2::mounts
  sandbox2::mount_tree_proto
  sapi::strerror
  sandbox2::util
  sandbox2::violation_proto
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2:forkingclient
add_library(sandbox2_forkingclient ${SAPI_LIB_TYPE}
  forkingclient.cc
  forkingclient.h
)
add_library(sandbox2::forkingclient ALIAS sandbox2_forkingclient)
target_link_libraries(sandbox2_forkingclient
  PRIVATE absl::memory
          absl::log
          sandbox2::sanitizer
          sapi::base
  PUBLIC sandbox2::client
         sandbox2::comms
         sandbox2::forkserver
)

# sandboxed_api/sandbox2:util
add_library(sandbox2_util ${SAPI_LIB_TYPE}
  util.cc
  util.h
)
add_library(sandbox2::util ALIAS sandbox2_util)
target_link_libraries(sandbox2_util
  PRIVATE absl::algorithm_container
          absl::core_headers
          absl::str_format
          absl::strings
          sapi::config
          sapi::file_base
          sapi::file_helpers
          sapi::fileops
          sapi::status
          sapi::base
          sapi::raw_logging
  PUBLIC absl::status
         absl::statusor
)
target_compile_options(sandbox2_util PRIVATE
  # The default is 16384, however we need to do a clone with a
  # stack-allocated buffer -- and PTHREAD_STACK_MIN also happens to be 16384.
  # Thus the slight increase.
  -Wframe-larger-than=17000
)

# sandboxed_api/sandbox2:buffer
add_library(sandbox2_buffer ${SAPI_LIB_TYPE}
  buffer.cc
  buffer.h
)
add_library(sandbox2::buffer ALIAS sandbox2_buffer)
target_link_libraries(sandbox2_buffer
  PRIVATE absl::core_headers
          absl::memory
          absl::status
          absl::strings
          sapi::strerror
          sandbox2::util
          sapi::base
          sapi::status
  PUBLIC absl::statusor
)

# sandboxed_api/sandbox2:forkserver_proto
sapi_protobuf_generate_cpp(_sandbox2_forkserver_pb_h _sandbox2_forkserver_pb_cc
  forkserver.proto
)
add_library(sandbox2_forkserver_proto ${SAPI_LIB_TYPE}
  ${_sandbox2_forkserver_pb_cc}
  ${_sandbox2_forkserver_pb_h}
)
add_library(sandbox2::forkserver_proto ALIAS sandbox2_forkserver_proto)
target_link_libraries(sandbox2_forkserver_proto PRIVATE
  protobuf::libprotobuf
  sandbox2::mount_tree_proto
  sapi::base
)

# sandboxed_api/sandbox2:mount_tree_proto
sapi_protobuf_generate_cpp(_sandbox2_mount_tree_pb_h _sandbox2_mount_tree_pb_cc
  mount_tree.proto
)
add_library(sandbox2_mount_tree_proto ${SAPI_LIB_TYPE}
  ${_sandbox2_mount_tree_pb_cc}
  ${_sandbox2_mount_tree_pb_h}
)
add_library(sandbox2::mount_tree_proto ALIAS sandbox2_mount_tree_proto)
target_link_libraries(sandbox2_mount_tree_proto PRIVATE
  protobuf::libprotobuf
  sapi::base
)

# sandboxed_api/sandbox2:comms
add_library(sandbox2_comms ${SAPI_LIB_TYPE}
  comms.cc
  comms.h
)
add_library(sandbox2::comms ALIAS sandbox2_comms)
target_link_libraries(sandbox2_comms
  PRIVATE absl::die_if_null
          absl::status
          absl::statusor
          absl::str_format
          absl::strings
          sapi::strerror
          sandbox2::util
          sapi::base
          sapi::raw_logging
          sapi::status_proto
  PUBLIC absl::core_headers
         absl::status
         absl::synchronization
         protobuf::libprotobuf
         sapi::status
)

# sandboxed_api/sandbox2:violation_proto
sapi_protobuf_generate_cpp(_sandbox2_violation_pb_cc _sandbox2_violation_pb_h
  violation.proto
)
add_library(sandbox2_violation_proto ${SAPI_LIB_TYPE}
  ${_sandbox2_violation_pb_cc}
  ${_sandbox2_violation_pb_h}
)
add_library(sandbox2::violation_proto ALIAS sandbox2_violation_proto)
target_link_libraries(sandbox2_violation_proto PRIVATE
  protobuf::libprotobuf
  sandbox2::mount_tree_proto
  sapi::base
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_subdirectory(testcases)

  # sandboxed_api/sandbox2:regs_test
  add_executable(sandbox2_regs_test
    regs_test.cc
  )
  set_target_properties(sandbox2_regs_test PROPERTIES
    OUTPUT_NAME regs_test
  )
  target_link_libraries(sandbox2_regs_test PRIVATE
    absl::check
    sapi::config
    sapi::status_matchers
    sandbox2::bpf_helper
    sandbox2::regs
    sandbox2::sanitizer
    sandbox2::util
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_regs_test)

  # sandboxed_api/sandbox2:syscall_test
  add_executable(sandbox2_syscall_test
    syscall_test.cc
  )
  set_target_properties(sandbox2_syscall_test PROPERTIES
    OUTPUT_NAME syscall_test
  )
  target_link_libraries(sandbox2_syscall_test PRIVATE
    absl::strings
    sapi::config
    sandbox2::syscall
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_syscall_test)

  # sandboxed_api/sandbox2:mounts_test
  add_executable(sandbox2_mounts_test
    mounts_test.cc
  )
  set_target_properties(sandbox2_mounts_test PROPERTIES
    OUTPUT_NAME mounts_test
  )
  add_dependencies(sandbox2_mounts_test
    sandbox2::testcase_minimal_dynamic
  )
  target_link_libraries(sandbox2_mounts_test PRIVATE
    absl::strings
    sapi::file_base
    sandbox2::mounts
    sandbox2::mount_tree_proto
    sapi::temp_file
    sapi::testing
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_mounts_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2:namespace_test
  add_executable(sandbox2_namespace_test
    namespace_test.cc
  )
  set_target_properties(sandbox2_namespace_test PROPERTIES
    OUTPUT_NAME namespace_test
  )
  add_dependencies(sandbox2_namespace_test
    sandbox2::testcase_hostname
    sandbox2::testcase_namespace
  )
  target_link_libraries(sandbox2_namespace_test PRIVATE
    absl::strings
    sandbox2::comms
    sapi::config
    sapi::fileops
    sandbox2::namespace
    sandbox2::sandbox2
    sapi::testing
    sapi::temp_file
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_namespace_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2:buffer_test
  add_executable(sandbox2_buffer_test
    buffer_test.cc
  )
  set_target_properties(sandbox2_buffer_test PROPERTIES
    OUTPUT_NAME buffer_test
  )
  add_dependencies(sandbox2_buffer_test
    sandbox2::testcase_buffer
  )
  target_link_libraries(sandbox2_buffer_test PRIVATE
    sandbox2::buffer
    sandbox2::comms
    sapi::config
    sandbox2::ipc
    sandbox2::sandbox2
    sapi::testing
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_buffer_test PROPERTIES
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2:comms_test_proto
  sapi_protobuf_generate_cpp(
    _sandbox2_comms_test_pb_h _sandbox2_comms_test_pb_cc
    comms_test.proto
  )
  add_library(sandbox2_comms_test_proto ${SAPI_LIB_TYPE}
    ${_sandbox2_comms_test_pb_cc}
    ${_sandbox2_comms_test_pb_h}
  )
  add_library(sandbox2::comms_test_proto ALIAS sandbox2_comms_test_proto)
  target_link_libraries(sandbox2_comms_test_proto
    PRIVATE sapi::base
    PUBLIC protobuf::libprotobuf
  )

  # sandboxed_api/sandbox2:comms_test
  add_executable(sandbox2_comms_test
    comms_test.cc
  )
  target_link_libraries(sandbox2_comms_test PRIVATE
    absl::check
    absl::fixed_array
    absl::log
    absl::strings
    sandbox2::comms
    sandbox2::comms_test_proto
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_comms_test)

  # sandboxed_api/sandbox2:forkserver_test
  add_executable(sandbox2_forkserver_test
    forkserver_test.cc
    global_forkclient.h
  )
  set_target_properties(sandbox2_forkserver_test PROPERTIES
    OUTPUT_NAME forkserver_test
  )
  add_dependencies(sandbox2_forkserver_test
    sandbox2::testcase_minimal
  )
  target_link_libraries(sandbox2_forkserver_test PRIVATE
    absl::check
    absl::strings
    sandbox2::comms
    sandbox2::forkserver
    sandbox2::forkserver_proto
    sandbox2::sandbox2
    sapi::raw_logging
    sapi::testing
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_forkserver_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2:limits_test
  add_executable(sandbox2_limits_test
    limits_test.cc
  )
  set_target_properties(sandbox2_limits_test PROPERTIES
    OUTPUT_NAME limits_test
  )
  add_dependencies(sandbox2_limits_test
    sandbox2::testcase_limits
    sandbox2::testcase_minimal
  )
  target_link_libraries(sandbox2_limits_test PRIVATE
    sandbox2::bpf_helper
    sapi::config
    sandbox2::limits
    sandbox2::sandbox2
    sapi::testing
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_limits_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2:notify_test
  add_executable(sandbox2_notify_test
    notify_test.cc
  )
  set_target_properties(sandbox2_notify_test PROPERTIES
    OUTPUT_NAME notify_test
  )
  add_dependencies(sandbox2_notify_test
    sandbox2::testcase_personality
    sandbox2::testcase_pidcomms
  )
  target_link_libraries(sandbox2_notify_test PRIVATE
    absl::strings
    sandbox2::bpf_helper
    sandbox2::comms
    sandbox2::regs
    sandbox2::sandbox2
    sapi::testing
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_notify_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2:policy_test
  add_executable(sandbox2_policy_test
    policy_test.cc
  )
  set_target_properties(sandbox2_policy_test PROPERTIES
    OUTPUT_NAME policy_test
  )
  add_dependencies(sandbox2_policy_test
    sandbox2::testcase_add_policy_on_syscalls
    sandbox2::testcase_malloc_system
    sandbox2::testcase_minimal
    sandbox2::testcase_minimal_dynamic
    sandbox2::testcase_policy
  )
  target_link_libraries(sandbox2_policy_test PRIVATE
    absl::strings
    sandbox2::bpf_helper
    sapi::config
    sandbox2::limits
    sandbox2::regs
    sandbox2::sandbox2
    sapi::testing
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_policy_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2:sandbox2_test
  add_executable(sandbox2_sandbox2_test
    sandbox2_test.cc
  )
  set_target_properties(sandbox2_sandbox2_test PROPERTIES
    OUTPUT_NAME sandbox2_test
  )
  add_dependencies(sandbox2_sandbox2_test
    sandbox2::testcase_abort
    sandbox2::testcase_minimal
    sandbox2::testcase_sleep
    sandbox2::testcase_tsync
  )
  target_link_libraries(sandbox2_sandbox2_test PRIVATE
    absl::strings
    sandbox2::bpf_helper
    sapi::config
    sandbox2::sandbox2
    sapi::testing
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_sandbox2_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2:sanitizer_test
  add_executable(sandbox2_sanitizer_test
    sanitizer_test.cc
  )
  set_target_properties(sandbox2_sanitizer_test PROPERTIES
    OUTPUT_NAME sanitizer_test
  )
  add_dependencies(sandbox2_sanitizer_test
    sandbox2::testcase_sanitizer
    sandbox2::testcase_close_fds
  )
  target_link_libraries(sandbox2_sanitizer_test PRIVATE
    absl::strings
    sandbox2::bpf_helper
    sandbox2::client
    sandbox2::comms
    sandbox2::sandbox2
    sandbox2::sanitizer
    sapi::testing
    sandbox2::util
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_sanitizer_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2:util_test
  add_executable(sandbox2_util_test
    util_test.cc
  )
  set_target_properties(sandbox2_util_test PROPERTIES
    OUTPUT_NAME util_test
  )
  target_link_libraries(sandbox2_util_test PRIVATE
    sapi::file_base
    sapi::testing
    sandbox2::util
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_util_test)

  # sandboxed_api/sandbox2:stack_trace_test
  add_executable(sandbox2_stack_trace_test
    stack_trace_test.cc
  )
  set_target_properties(sandbox2_stack_trace_test PROPERTIES
    OUTPUT_NAME stack_trace_test
  )
  add_dependencies(sandbox2_stack_trace_test
    sandbox2::testcase_symbolize
  )
  target_link_libraries(sandbox2_stack_trace_test PRIVATE
    absl::cleanup
    absl::flags
    absl::status
    absl::strings
    sandbox2::bpf_helper
    sandbox2::global_forkserver
    sandbox2::sandbox2
    sandbox2::util
    sapi::fileops
    sapi::temp_file
    sapi::testing
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_stack_trace_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2:ipc_test
  add_executable(sandbox2_ipc_test
    ipc_test.cc
  )
  set_target_properties(sandbox2_ipc_test PROPERTIES
    OUTPUT_NAME ipc_test
  )
  add_dependencies(sandbox2_ipc_test
    sandbox2::testcase_ipc
  )
  target_link_libraries(sandbox2_ipc_test PRIVATE
    sandbox2::bpf_helper
    sandbox2::comms
    sandbox2::ipc
    sandbox2::sandbox2
    sapi::testing
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_ipc_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/sandbox2:policybuilder_test
  add_executable(sandbox2_policybuilder_test
    policybuilder_test.cc
  )
  set_target_properties(sandbox2_policybuilder_test PROPERTIES
    OUTPUT_NAME policybuilder_test
  )
  add_dependencies(sandbox2_policybuilder_test
    sandbox2::testcase_print_fds
  )
  target_link_libraries(sandbox2_policybuilder_test
    PRIVATE absl::strings
            sandbox2::bpf_helper
            sandbox2::comms
            sapi::testing
            sapi::status_matchers
            sapi::test_main
    PUBLIC sandbox2::sandbox2
  )
  gtest_discover_tests_xcompile(sandbox2_policybuilder_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )
endif()

configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/sandbox2.pc.in"
  "${PROJECT_BINARY_DIR}/sandbox2.pc"
  @ONLY
)

install(FILES "${PROJECT_BINARY_DIR}/sandbox2.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
