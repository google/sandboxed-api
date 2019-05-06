# Copyright 2019 Google LLC. All Rights Reserved.
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

add_subdirectory(testcases)
add_subdirectory(unwind)
add_subdirectory(util)

# sandboxed_api/sandbox2:bpfdisassembler
add_library(sandbox2_bpfdisassembler STATIC
  bpfdisassembler.cc
  bpfdisassembler.h
)
add_library(sandbox2::bpfdisassembler ALIAS sandbox2_bpfdisassembler)
target_link_libraries(sandbox2_bpfdisassembler PRIVATE
  absl::strings
  sapi::base
)

# sandboxed_api/sandbox2:regs
add_library(sandbox2_regs STATIC
  regs.cc
  regs.h
)
add_library(sandbox2::regs ALIAS sandbox2_regs)
target_link_libraries(sandbox2_regs PRIVATE
  absl::core_headers
  absl::strings
  sandbox2::strerror
  sandbox2::syscall
  sandbox2::violation_proto
  sapi::base
  sapi::status
)

# sandboxed_api/sandbox2:syscall
add_library(sandbox2_syscall STATIC
  syscall.cc
  syscall.h
  syscall_defs.cc
  syscall_defs.h
)
add_library(sandbox2::syscall ALIAS sandbox2_syscall)
target_link_libraries(sandbox2_syscall
  PRIVATE absl::span
          absl::str_format
          absl::strings
          sandbox2::util
          sapi::base
  PUBLIC  glog::glog
)

# sandboxed_api/sandbox2:syscall_test
add_executable(syscall_test
  syscall_test.cc
)
target_link_libraries(syscall_test PRIVATE
  absl::strings
  sandbox2::syscall
  sapi::test_main
)
gtest_discover_tests(syscall_test)

# sandboxed_api/sandbox2:result
add_library(sandbox2_result STATIC
  result.cc
  result.h
)
add_library(sandbox2::result ALIAS sandbox2_result)
target_link_libraries(sandbox2_result PRIVATE
  absl::base
  absl::memory
  absl::strings
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
add_library(sandbox2_logserver_proto STATIC
  ${_sandbox2_logserver_pb_cc}
  ${_sandbox2_logserver_pb_h}
)
add_library(sandbox2::logserver_proto ALIAS sandbox2_logserver_proto)
target_link_libraries(sandbox2_logserver_proto PRIVATE
  protobuf::libprotobuf
  sapi::base
)

# sandboxed_api/sandbox2:logserver
add_library(sandbox2_logserver STATIC
  logserver.cc
  logserver.h
)
add_library(sandbox2::logserver ALIAS sandbox2_logserver)
target_link_libraries(sandbox2_logserver
  PRIVATE absl::memory
          sandbox2::comms
          sandbox2::logserver_proto
          sapi::base
  PUBLIC  glog::glog
)

# sandboxed_api/sandbox2:logsink
add_library(sandbox2_logsink STATIC
  logsink.cc
  logsink.h
)
add_library(sandbox2::logsink ALIAS sandbox2_logsink)
target_link_libraries(sandbox2_logsink
  PRIVATE absl::strings
          absl::synchronization
          sandbox2::comms
          sandbox2::logserver_proto
          sapi::base
  PUBLIC  glog::glog
)

# sandboxed_api/sandbox2:network_proxy_server
add_library(sandbox2_network_proxy_server STATIC
  network_proxy_server.cc
  network_proxy_server.h
)
add_library(sandbox2::network_proxy_server ALIAS sandbox2_network_proxy_server)
target_link_libraries(sandbox2_network_proxy_server PRIVATE
  absl::memory
  glog::glog
  sandbox2::comms
  sandbox2::fileops
  sapi::base
)

# sandboxed_api/sandbox2:network_proxy_client
add_library(sandbox2_network_proxy_client STATIC
  network_proxy_client.cc
  network_proxy_client.h
)
add_library(sandbox2::network_proxy_client ALIAS sandbox2_network_proxy_client)
target_link_libraries(sandbox2_network_proxy_client PRIVATE
  absl::strings
  absl::synchronization
  glog::glog
  sandbox2::comms
  sandbox2::strerror
  sapi::base
  sapi::status
)

# sandboxed_api/sandbox2:ipc
add_library(sandbox2_ipc STATIC
  ipc.cc
  ipc.h
)
add_library(sandbox2::ipc ALIAS sandbox2_ipc)
target_link_libraries(sandbox2_ipc PRIVATE
  absl::core_headers
  absl::memory
  absl::strings
  sandbox2::comms
  sandbox2::logserver
  sandbox2::logsink
  sandbox2::network_proxy_client
  sandbox2::network_proxy_server
  sapi::base
)

# sandboxed_api/sandbox2:policy
add_library(sandbox2_policy STATIC
  policy.cc
  policy.h
)
add_library(sandbox2::policy ALIAS sandbox2_policy)
target_link_libraries(sandbox2_policy PRIVATE
  absl::core_headers
  absl::optional
  libcap::libcap
  sandbox2::bpf_helper
  sandbox2::bpfdisassembler
  sandbox2::comms
  sandbox2::namespace
  sandbox2::regs
  sandbox2::syscall
  sandbox2::violation_proto
  sapi::base
  sapi::flag
)

# sandboxed_api/sandbox2:notify
add_library(sandbox2_notify STATIC
  notify.h
)
add_library(sandbox2::notify ALIAS sandbox2_notify)
target_link_libraries(sandbox2_notify PRIVATE
  sandbox2::comms
  sandbox2::result
  sandbox2::syscall
  sapi::base
)

# sandboxed_api/sandbox2:limits
add_library(sandbox2_limits STATIC
  limits.h
)
add_library(sandbox2::limits ALIAS sandbox2_limits)
target_link_libraries(sandbox2_limits PRIVATE
  absl::core_headers
  absl::time
  sapi::base
)

# sandboxed_api/sandbox2:forkserver_bin
add_executable(forkserver_bin  # Need unprefixed name here
  forkserver_bin.cc
)
add_executable(sandbox2_forkserver_bin ALIAS forkserver_bin)
add_executable(sandbox2::forkserver_bin ALIAS forkserver_bin)
target_link_libraries(forkserver_bin PRIVATE
  absl::core_headers
  absl::strings
  sandbox2::client
  sandbox2::comms
  sandbox2::forkserver
  sandbox2::strerror
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2:forkserver_bin_embed
sapi_cc_embed_data(NAME forkserver_bin_embed  # Need unprefixed name here
  NAMESPACE ""
  SOURCES forkserver_bin
)
add_library(sandbox2_forkserver_bin_embed ALIAS forkserver_bin_embed)
add_library(sandbox2::forkserver_bin_embed ALIAS forkserver_bin_embed)

# sandboxed_api/sandbox2:global_forkserver
add_library(sandbox2_global_forkserver STATIC
  global_forkclient.cc
  global_forkclient.h
)
add_library(sandbox2::global_forkserver ALIAS sandbox2_global_forkserver)
target_link_libraries(sandbox2_global_forkserver PRIVATE
  absl::core_headers
  absl::strings
  sandbox2::client
  sandbox2::comms
  sandbox2::forkserver
  sandbox2::forkserver_bin_embed
  sandbox2::strerror
  sapi::base
  sapi::embed_file
  sapi::raw_logging
)

# sandboxed_api/sandbox2:sandbox2
add_library(sandbox2_sandbox2 STATIC
  executor.cc
  executor.h
  monitor.cc
  monitor.h
  policybuilder.cc
  policybuilder.h
  sandbox2.cc
  sandbox2.h
  stack-trace.cc
  stack-trace.h
)
add_library(sandbox2::sandbox2 ALIAS sandbox2_sandbox2)
target_link_libraries(sandbox2_sandbox2
  PRIVATE absl::core_headers
          absl::flat_hash_map
          absl::flat_hash_set
          absl::memory
          absl::optional
          absl::str_format
          absl::strings
          absl::synchronization
          absl::time
          libcap::libcap
          sandbox2::bpf_helper
          sandbox2::client
          sandbox2::comms
          sandbox2::file_base
          sandbox2::fileops
          sandbox2::forkserver
          sandbox2::forkserver_proto
          sandbox2::global_forkserver
          sandbox2::ipc
          sandbox2::limits
          sandbox2::mounts
          sandbox2::namespace
          sandbox2::network_proxy_client
          sandbox2::notify
          sandbox2::policy
          sandbox2::ptrace_hook
          sandbox2::regs
          sandbox2::result
          sandbox2::syscall
          sandbox2::unwind
          sandbox2::unwind_proto
          sandbox2::util
          sandbox2::violation_proto
          sapi::base
          sapi::status
          sapi::statusor
  PUBLIC  sapi::flag
          sandbox2::logsink
)

# sandboxed_api/sandbox2:client
add_library(sandbox2_client STATIC
  client.cc
  client.h
  sanitizer.cc
  sanitizer.h
)
add_library(sandbox2::client ALIAS sandbox2_client)
target_link_libraries(sandbox2_client
  PRIVATE absl::core_headers
          absl::memory
          absl::strings
          sandbox2::comms
          sandbox2::file_helpers
          sandbox2::fileops
          sandbox2::logsink
          sandbox2::network_proxy_client
          sandbox2::strerror
          sapi::base
          sapi::raw_logging
  PUBLIC  glog::glog
)

# sandboxed_api/sandbox2:forkserver
add_library(sandbox2_forkserver STATIC
  forkserver.cc
  forkserver.h
)
add_library(sandbox2::forkserver ALIAS sandbox2_forkserver)
target_link_libraries(sandbox2_forkserver PRIVATE
  absl::str_format
  absl::strings
  absl::synchronization
  libcap::libcap
  sandbox2::bpf_helper
  sandbox2::client
  sandbox2::comms
  sandbox2::fileops
  sandbox2::forkserver_proto
  sandbox2::namespace
  sandbox2::policy
  sandbox2::ptrace_hook
  sandbox2::strerror
  sandbox2::syscall
  sandbox2::unwind
  sandbox2::unwind_proto
  sandbox2::util
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2:mounts
add_library(sandbox2_mounts STATIC
  mounts.cc
  mounts.h
)
add_library(sandbox2::mounts ALIAS sandbox2_mounts)
target_link_libraries(sandbox2_mounts PRIVATE
  absl::core_headers
  absl::flat_hash_set
  absl::str_format
  absl::strings
  protobuf::libprotobuf
  sandbox2::file_base
  sandbox2::fileops
  sandbox2::minielf
  sandbox2::mounttree_proto
  sandbox2::strerror
  sapi::base
  sapi::raw_logging
  sapi::status
  sapi::statusor
)

# sandboxed_api/sandbox2:mounts_test
add_executable(mounts_test
  mounts_test.cc
)
add_dependencies(mounts_test
  sandbox2::testcase_minimal_dynamic
)
target_link_libraries(mounts_test PRIVATE
  absl::strings
  sandbox2::file_base
  sandbox2::mounts
  sandbox2::temp_file
  sandbox2::testing
  sapi::status_matchers
  sapi::test_main
)
gtest_discover_tests(mounts_test PROPERTIES
  ENVIRONMENT "TEST_TMPDIR=/tmp"
  ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
)

# sandboxed_api/sandbox2:namespace
add_library(sandbox2_namespace STATIC
  namespace.cc
  namespace.h
)
add_library(sandbox2::namespace ALIAS sandbox2_namespace)
target_link_libraries(sandbox2_namespace PRIVATE
  absl::core_headers
  absl::str_format
  absl::strings
  sandbox2::file_base
  sandbox2::fileops
  sandbox2::mounts
  sandbox2::mounttree_proto
  sandbox2::strerror
  sandbox2::util
  sandbox2::violation_proto
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2:namespace_test
add_executable(namespace_test
  namespace_test.cc
)
add_dependencies(mounts_test
  sandbox2::testcase_hostname
  sandbox2::testcase_namespace
)
target_link_libraries(namespace_test PRIVATE
  absl::memory
  absl::strings
  sandbox2::comms
  sandbox2::namespace
  sandbox2::sandbox2
  sandbox2::testing
  sapi::status_matchers
  sapi::test_main
)
gtest_discover_tests(namespace_test PROPERTIES
  ENVIRONMENT "TEST_TMPDIR=/tmp"
  ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
)

# sandboxed_api/sandbox2:forkingclient
add_library(sandbox2_forkingclient STATIC
  forkingclient.cc
  forkingclient.h
)
add_library(sandbox2::forkingclient ALIAS sandbox2_forkingclient)
target_link_libraries(sandbox2_forkingclient PRIVATE
  absl::memory
  sandbox2::client
  sandbox2::forkserver
  sapi::base
)

# sandboxed_api/sandbox2:util
add_library(sandbox2_util STATIC
  util.cc
  util.h
)
add_library(sandbox2::util ALIAS sandbox2_util)
target_link_libraries(sandbox2_util PRIVATE
  absl::core_headers
  absl::str_format
  absl::strings
  sandbox2::file_base
  sandbox2::fileops
  sandbox2::strerror
  sapi::base
  sapi::raw_logging
  sapi::status
  sapi::statusor
)
target_compile_options(sandbox2_util PRIVATE
  # The default is 16384, however we need to do a clone with a
  # stack-allocated buffer -- and PTHREAD_STACK_MIN also happens to be 16384.
  # Thus the slight increase.
  -Wframe-larger-than=17000
)

# sandboxed_api/sandbox2:buffer
add_library(sandbox2_buffer STATIC
  buffer.cc
  buffer.h
)
add_library(sandbox2::buffer ALIAS sandbox2_buffer)
target_link_libraries(sandbox2_buffer PRIVATE
  absl::core_headers
  absl::memory
  absl::strings
  sandbox2::strerror
  sandbox2::util
  sapi::base
  sapi::status
  sapi::statusor
)

# sandboxed_api/sandbox2:buffer_test
add_executable(buffer_test
  buffer_test.cc
)
add_dependencies(buffer_test
  sandbox2::testcase_buffer
)
target_link_libraries(buffer_test PRIVATE
  absl::memory
  sandbox2::buffer
  sandbox2::comms
  sandbox2::sandbox2
  sandbox2::testing
  sapi::status_matchers
  sapi::test_main
)
gtest_discover_tests(buffer_test)

# sandboxed_api/sandbox2:forkserver_proto
sapi_protobuf_generate_cpp(_sandbox2_forkserver_pb_h _sandbox2_forkserver_pb_cc
  forkserver.proto
)
add_library(sandbox2_forkserver_proto STATIC
  ${_sandbox2_forkserver_pb_cc}
  ${_sandbox2_forkserver_pb_h}
)
add_library(sandbox2::forkserver_proto ALIAS sandbox2_forkserver_proto)
target_link_libraries(sandbox2_forkserver_proto PRIVATE
  protobuf::libprotobuf
  sandbox2::mounttree_proto
  sapi::base
)

# sandboxed_api/sandbox2:mounttree_proto
sapi_protobuf_generate_cpp(_sandbox2_mounttree_pb_h _sandbox2_mounttree_pb_cc
  mounttree.proto
)
add_library(sandbox2_mounttree_proto STATIC
  ${_sandbox2_mounttree_pb_cc}
  ${_sandbox2_mounttree_pb_h}
)
add_library(sandbox2::mounttree_proto ALIAS sandbox2_mounttree_proto)
target_link_libraries(sandbox2_mounttree_proto PRIVATE
  protobuf::libprotobuf
  sapi::base
)

# sandboxed_api/sandbox2:comms
add_library(sandbox2_comms STATIC
  comms.cc
  comms.h
)
add_library(sandbox2::comms ALIAS sandbox2_comms)
target_link_libraries(sandbox2_comms PRIVATE
  absl::core_headers
  absl::memory
  absl::str_format
  absl::strings
  absl::synchronization
  protobuf::libprotobuf
  sandbox2::strerror
  sandbox2::util
  sapi::base
  sapi::raw_logging
  sapi::status
  sapi::status_proto
  sapi::statusor
)

# sandboxed_api/sandbox2:comms_test_proto
sapi_protobuf_generate_cpp(_sandbox2_comms_test_pb_h _sandbox2_comms_test_pb_cc
  comms_test.proto
)
add_library(sandbox2_comms_test_proto STATIC
  ${_sandbox2_comms_test_pb_cc}
  ${_sandbox2_comms_test_pb_h}
)
add_library(sandbox2::comms_test_proto ALIAS sandbox2_comms_test_proto)
target_link_libraries(sandbox2_comms_test_proto PRIVATE
  protobuf::libprotobuf
  sapi::base
)

# sandboxed_api/sandbox2:comms_test
add_executable(comms_test
  comms_test.cc
)
target_link_libraries(comms_test PRIVATE
  absl::fixed_array
  absl::strings
  glog::glog
  protobuf::libprotobuf
  sandbox2::comms
  sandbox2::comms_test_proto
  sapi::status_matchers
  sapi::test_main
)
gtest_discover_tests(comms_test)

# sandboxed_api/sandbox2:forkserver_test
add_executable(forkserver_test
  forkserver_test.cc
  global_forkclient.h
)
add_dependencies(forkserver_test
  sandbox2::testcase_minimal
)
target_link_libraries(forkserver_test PRIVATE
  absl::strings
  glog::glog
  sandbox2::comms
  sandbox2::forkserver
  sandbox2::forkserver_proto
  sandbox2::sandbox2
  sandbox2::testing
  sapi::test_main
)
gtest_discover_tests(forkserver_test)

# sandboxed_api/sandbox2:limits_test
add_executable(limits_test
  limits_test.cc
)
add_dependencies(limits_test
  sandbox2::testcase_minimal
)
target_link_libraries(limits_test PRIVATE
  absl::memory
  sandbox2::bpf_helper
  sandbox2::limits
  sandbox2::sandbox2
  sandbox2::testing
  sapi::status_matchers
  sapi::test_main
)
gtest_discover_tests(limits_test)

# sandboxed_api/sandbox2:notify_test
add_executable(notify_test
  notify_test.cc
)
add_dependencies(notify_test
  sandbox2::testcase_personality
  sandbox2::testcase_pidcomms
)
target_link_libraries(notify_test PRIVATE
  absl::memory
  absl::strings
  sandbox2::bpf_helper
  sandbox2::comms
  sandbox2::regs
  sandbox2::sandbox2
  sandbox2::testing
  sapi::test_main
)
gtest_discover_tests(notify_test PROPERTIES
  ENVIRONMENT "TEST_TMPDIR=/tmp"
  ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
)

# sandboxed_api/sandbox2:policy_test
add_executable(policy_test
  policy_test.cc
)
add_dependencies(policy_test
  sandbox2::testcase_add_policy_on_syscalls
  sandbox2::testcase_malloc_system
  sandbox2::testcase_minimal
  sandbox2::testcase_minimal_dynamic
  sandbox2::testcase_policy
)
target_link_libraries(policy_test PRIVATE
  absl::memory
  absl::strings
  sandbox2::bpf_helper
  sandbox2::limits
  sandbox2::regs
  sandbox2::sandbox2
  sandbox2::testing
  sapi::test_main
)
gtest_discover_tests(policy_test PROPERTIES
  ENVIRONMENT "TEST_TMPDIR=/tmp"
  ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
)

# sandboxed_api/sandbox2:sandbox2_test
add_executable(sandbox2_test
  sandbox2_test.cc
)
add_dependencies(sandbox2_test
  sandbox2::testcase_abort
  sandbox2::testcase_minimal
  sandbox2::testcase_sleep
  sandbox2::testcase_tsync
)
target_link_libraries(sandbox2_test PRIVATE
  absl::memory
  absl::strings
  sandbox2::bpf_helper
  sandbox2::sandbox2
  sandbox2::testing
  sapi::status_matchers
  sapi::test_main
)
gtest_discover_tests(sandbox2_test PROPERTIES
  ENVIRONMENT "TEST_TMPDIR=/tmp"
  ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
)

# sandboxed_api/sandbox2:sanitizer_test
add_executable(sanitizer_test
  sanitizer_test.cc
)
add_dependencies(sanitizer_test
  sandbox2::testcase_sanitizer
)
target_link_libraries(sanitizer_test PRIVATE
  absl::memory
  absl::strings
  sandbox2::bpf_helper
  sandbox2::client
  sandbox2::comms
  sandbox2::sandbox2
  sandbox2::testing
  sandbox2::util
  sapi::status_matchers
  sapi::test_main
)
gtest_discover_tests(sanitizer_test PROPERTIES
  ENVIRONMENT "TEST_TMPDIR=/tmp"
  ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
)

# sandboxed_api/sandbox2:util_test
add_executable(util_test
  util_test.cc
)
target_link_libraries(util_test PRIVATE
  sandbox2::file_base
  sandbox2::testing
  sandbox2::util
  sapi::test_main
)
gtest_discover_tests(util_test)

# sandboxed_api/sandbox2:stack_trace_test
add_executable(stack-trace_test
  stack-trace_test.cc
)
add_dependencies(stack-trace_test
  sandbox2::testcase_symbolize
)
target_link_libraries(stack-trace_test PRIVATE
  absl::memory
  absl::strings
  sandbox2::bpf_helper
  sandbox2::fileops
  sandbox2::global_forkserver
  sandbox2::sandbox2
  sandbox2::temp_file
  sandbox2::testing
  sandbox2::util
  sapi::status_matchers
  sapi::test_main
  sapi::flag
)
gtest_discover_tests(stack-trace_test PROPERTIES
  ENVIRONMENT "TEST_TMPDIR=/tmp"
  ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
)

# sandboxed_api/sandbox2:ipc_test
add_executable(ipc_test
  ipc_test.cc
)
add_dependencies(ipc_test
  sandbox2::testcase_ipc
)
target_link_libraries(ipc_test PRIVATE
  absl::memory
  sandbox2::bpf_helper
  sandbox2::comms
  sandbox2::sandbox2
  sandbox2::testing
  sapi::status_matchers
  sapi::test_main
  sapi::flag
)
gtest_discover_tests(ipc_test PROPERTIES
  ENVIRONMENT "TEST_TMPDIR=/tmp"
  ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
)

# sandboxed_api/sandbox2:testing
add_library(sandbox2_testing STATIC
  testing.cc
  testing.h
)
add_library(sandbox2::testing ALIAS sandbox2_testing)
target_link_libraries(sandbox2_testing PRIVATE
  absl::strings
  sandbox2::file_base
  sapi::base
)

# sandboxed_api/sandbox2:violation_proto
sapi_protobuf_generate_cpp(_sandbox2_violation_pb_cc _sandbox2_violation_pb_h
  violation.proto
)
add_library(sandbox2_violation_proto STATIC
  ${_sandbox2_violation_pb_cc}
  ${_sandbox2_violation_pb_h}
)
add_library(sandbox2::violation_proto ALIAS sandbox2_violation_proto)
target_link_libraries(sandbox2_violation_proto PRIVATE
  protobuf::libprotobuf
  sandbox2::mounttree_proto
  sapi::base
)

# sandboxed_api/sandbox2:policybuilder_test
add_executable(policybuilder_test
  policybuilder_test.cc
)
add_dependencies(policybuilder_test
  sandbox2::testcase_print_fds
)
target_link_libraries(policybuilder_test PRIVATE
  absl::memory
  absl::strings
  glog::glog
  sandbox2::bpf_helper
  sandbox2::comms
  sandbox2::sandbox2
  sandbox2::testing
  sapi::status_matchers
  sapi::test_main
  sapi::flag
)
gtest_discover_tests(policybuilder_test PROPERTIES
  ENVIRONMENT "TEST_TMPDIR=/tmp"
  ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
)
