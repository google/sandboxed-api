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

# sandboxed_api/sandbox2/testcases:abort
add_executable(sandbox2_testcase_abort
  abort.cc
)
set_target_properties(sandbox2_testcase_abort PROPERTIES
  OUTPUT_NAME abort)
add_executable(sandbox2::testcase_abort ALIAS sandbox2_testcase_abort)
target_link_libraries(sandbox2_testcase_abort PRIVATE
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2/testcases:add_policy_on_syscalls
add_executable(sandbox2_testcase_add_policy_on_syscalls
  add_policy_on_syscalls.cc
)
add_executable(sandbox2::testcase_add_policy_on_syscalls
  ALIAS sandbox2_testcase_add_policy_on_syscalls)
set_target_properties(sandbox2_testcase_add_policy_on_syscalls PROPERTIES
  OUTPUT_NAME add_policy_on_syscalls
)
target_link_libraries(sandbox2_testcase_add_policy_on_syscalls PRIVATE
  -static
  sapi::base
)

# sandboxed_api/sandbox2/testcases:buffer
add_executable(sandbox2_testcase_buffer
  buffer.cc
)
add_executable(sandbox2::testcase_buffer ALIAS sandbox2_testcase_buffer)
set_target_properties(sandbox2_testcase_buffer PROPERTIES
  OUTPUT_NAME buffer
)
target_link_libraries(sandbox2_testcase_buffer PRIVATE
  absl::str_format
  sandbox2::buffer
  sandbox2::comms
  sapi::base
)

# sandboxed_api/sandbox2/testcases:ipc
add_executable(sandbox2_testcase_ipc
  ipc.cc
)
set_target_properties(sandbox2_testcase_ipc PROPERTIES
  OUTPUT_NAME ipc
)
add_executable(sandbox2::testcase_ipc ALIAS sandbox2_testcase_ipc)
target_link_libraries(sandbox2_testcase_ipc PRIVATE
  absl::strings
  sandbox2::client
  sandbox2::comms
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2/testcases:malloc_system
add_executable(sandbox2_testcase_malloc_system
  malloc.cc
)
add_executable(sandbox2::testcase_malloc_system ALIAS
  sandbox2_testcase_malloc_system)
set_target_properties(sandbox2_testcase_malloc_system PROPERTIES
  OUTPUT_NAME malloc_system
)
target_link_libraries(sandbox2_testcase_malloc_system PRIVATE
  -static
  sapi::base
)

# sandboxed_api/sandbox2/testcases:minimal_dynamic
add_executable(sandbox2_testcase_minimal_dynamic
  minimal.cc
)
set_target_properties(sandbox2_testcase_minimal_dynamic PROPERTIES
  OUTPUT_NAME minimal_dynamic
)
add_executable(sandbox2::testcase_minimal_dynamic ALIAS
  sandbox2_testcase_minimal_dynamic)
target_link_libraries(sandbox2_testcase_minimal_dynamic PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2/testcases:minimal
add_executable(sandbox2_testcase_minimal
  minimal.cc
)
add_executable(sandbox2::testcase_minimal ALIAS sandbox2_testcase_minimal)
set_target_properties(sandbox2_testcase_minimal PROPERTIES
  OUTPUT_NAME minimal
)
target_link_libraries(sandbox2_testcase_minimal PRIVATE
  -static
  sapi::base
)

# sandboxed_api/sandbox2/testcases:personality
add_executable(sandbox2_testcase_personality
  personality.cc
)
add_executable(sandbox2::testcase_personality ALIAS
  sandbox2_testcase_personality)
set_target_properties(sandbox2_testcase_personality PROPERTIES
  OUTPUT_NAME personality
)
target_link_libraries(sandbox2_testcase_personality PRIVATE
  -static
  sapi::base
)

# sandboxed_api/sandbox2/testcases:pidcomms
add_executable(sandbox2_testcase_pidcomms
  pidcomms.cc
)
add_executable(sandbox2::testcase_pidcomms ALIAS sandbox2_testcase_pidcomms)
set_target_properties(sandbox2_testcase_pidcomms PROPERTIES
  OUTPUT_NAME pidcomms
)
target_link_libraries(sandbox2_testcase_pidcomms PRIVATE
  sandbox2::client
  sandbox2::comms
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2/testcases:policy
add_executable(sandbox2_testcase_policy
  policy.cc
)
add_executable(sandbox2::testcase_policy ALIAS sandbox2_testcase_policy)
set_target_properties(sandbox2_testcase_policy PROPERTIES
  OUTPUT_NAME policy
)
target_link_libraries(sandbox2_testcase_policy PRIVATE
  -static
  sapi::base
  sapi::config
)

# sandboxed_api/sandbox2/testcases:print_fds
add_executable(sandbox2_testcase_print_fds
  print_fds.cc
)
add_executable(sandbox2::testcase_print_fds ALIAS sandbox2_testcase_print_fds)
set_target_properties(sandbox2_testcase_print_fds PROPERTIES
  OUTPUT_NAME print_fds
)
target_link_libraries(sandbox2_testcase_print_fds PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2/testcases:sanitizer
add_executable(sandbox2_testcase_sanitizer
  sanitizer.cc
)
add_executable(sandbox2::testcase_sanitizer ALIAS sandbox2_testcase_sanitizer)
set_target_properties(sandbox2_testcase_sanitizer PROPERTIES
  OUTPUT_NAME sanitizer
)
target_link_libraries(sandbox2_testcase_sanitizer PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2/testcases:close_fds
add_executable(sandbox2_testcase_close_fds
  close_fds.cc
)
add_executable(sandbox2::testcase_close_fds ALIAS sandbox2_testcase_close_fds)
set_target_properties(sandbox2_testcase_close_fds PROPERTIES
  OUTPUT_NAME close_fds
)
target_link_libraries(sandbox2_testcase_close_fds PRIVATE
  sapi::base
  absl::check
  absl::strings
  absl::flat_hash_set
  sandbox2::sanitizer
)


# sandboxed_api/sandbox2/testcases:sleep
add_executable(sandbox2_testcase_sleep
  sleep.cc
)
add_executable(sandbox2::testcase_sleep ALIAS sandbox2_testcase_sleep)
set_target_properties(sandbox2_testcase_sleep PROPERTIES
  OUTPUT_NAME sleep
)
target_link_libraries(sandbox2_testcase_sleep PRIVATE
  -static
  sapi::base
)

# sandboxed_api/sandbox2/testcases:symbolize
add_executable(sandbox2_testcase_symbolize
  symbolize.cc
)
add_executable(sandbox2::testcase_symbolize ALIAS sandbox2_testcase_symbolize)
set_target_properties(sandbox2_testcase_symbolize PROPERTIES
  OUTPUT_NAME symbolize
)
target_link_libraries(sandbox2_testcase_symbolize PRIVATE
  absl::core_headers
  absl::strings
  sapi::temp_file
  sapi::strerror
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2/testcases:starve
add_executable(sandbox2_testcase_starve
  starve.cc
)
add_executable(sandbox2::testcase_starve ALIAS sandbox2_testcase_starve)
set_target_properties(sandbox2_testcase_starve PROPERTIES
  OUTPUT_NAME starve
)
target_link_libraries(sandbox2_testcase_starve PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2/testcases:tsync
add_executable(sandbox2_testcase_tsync
  tsync.cc
)
add_executable(sandbox2::testcase_tsync ALIAS sandbox2_testcase_tsync)
set_target_properties(sandbox2_testcase_tsync PROPERTIES
  OUTPUT_NAME tsync
)
target_link_libraries(sandbox2_testcase_tsync PRIVATE
  sandbox2::client
  sandbox2::comms
  sapi::base
)

# sandboxed_api/sandbox2/testcases:hostname
add_executable(sandbox2_testcase_hostname
  hostname.cc
)
add_executable(sandbox2::testcase_hostname ALIAS sandbox2_testcase_hostname)
set_target_properties(sandbox2_testcase_hostname PROPERTIES
  OUTPUT_NAME hostname
)
target_link_libraries(sandbox2_testcase_hostname PRIVATE
  -static
  sapi::base
)

# sandboxed_api/sandbox2/testcases:limits
add_executable(sandbox2_testcase_limits
  limits.cc
)
add_executable(sandbox2::testcase_limits ALIAS sandbox2_testcase_limits)
set_target_properties(sandbox2_testcase_limits PROPERTIES
  OUTPUT_NAME limits
)
target_link_libraries(sandbox2_testcase_limits PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2/testcases:namespace
add_executable(sandbox2_testcase_namespace
  namespace.cc
)
add_executable(sandbox2::testcase_namespace ALIAS sandbox2_testcase_namespace)
set_target_properties(sandbox2_testcase_namespace PROPERTIES
  OUTPUT_NAME namespace
)
target_link_libraries(sandbox2_testcase_namespace PRIVATE
  -static
  sapi::base
)
