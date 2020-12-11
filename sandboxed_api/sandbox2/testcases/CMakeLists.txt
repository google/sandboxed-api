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

set(_sandbox2_fully_static_linkopts -static)

# sandboxed_api/sandbox2/testcases:abort
add_executable(abort
  abort.cc
)
add_executable(sandbox2::testcase_abort ALIAS abort)
target_link_libraries(abort PRIVATE
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2/testcases:add_policy_on_syscalls
add_executable(add_policy_on_syscalls
  add_policy_on_syscalls.cc
)
add_executable(sandbox2::testcase_add_policy_on_syscalls
  ALIAS add_policy_on_syscalls)
target_link_libraries(add_policy_on_syscalls PRIVATE
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:buffer
add_executable(buffer
  buffer.cc
)
add_executable(sandbox2::testcase_buffer ALIAS buffer)
target_link_libraries(buffer PRIVATE
  absl::str_format
  sandbox2::buffer
  sandbox2::comms
  sapi::base
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:ipc
add_executable(ipc
  ipc.cc
)
add_executable(sandbox2::testcase_ipc ALIAS ipc)
target_link_libraries(ipc PRIVATE
  absl::strings
  -Wl,--whole-archive
  gflags::gflags
  -Wl,--no-whole-archive
  glog::glog
  sandbox2::client
  sandbox2::comms
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/sandbox2/testcases:malloc_system
add_executable(malloc_system
  malloc.cc
)
add_executable(sandbox2::testcase_malloc_system ALIAS malloc_system)
target_link_libraries(malloc_system PRIVATE
  sapi::base
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:minimal_dynamic
add_executable(minimal_dynamic
  minimal.cc
)
add_executable(sandbox2::testcase_minimal_dynamic ALIAS minimal_dynamic)
target_link_libraries(minimal_dynamic PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2/testcases:minimal
add_executable(minimal
  minimal.cc
)
add_executable(sandbox2::testcase_minimal ALIAS minimal)
target_link_libraries(minimal PRIVATE
  sapi::base
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:personality
add_executable(personality
  personality.cc
)
add_executable(sandbox2::testcase_personality ALIAS personality)
target_link_libraries(personality PRIVATE
  sapi::base
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:pidcomms
add_executable(pidcomms
  pidcomms.cc
)
add_executable(sandbox2::testcase_pidcomms ALIAS pidcomms)
target_link_libraries(pidcomms PRIVATE
  -Wl,--whole-archive
  gflags::gflags
  -Wl,--no-whole-archive
  glog::glog
  sandbox2::client
  sandbox2::comms
  sapi::base
  sapi::raw_logging
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:policy
add_executable(policy
  policy.cc
)
add_executable(sandbox2::testcase_policy ALIAS policy)
target_link_libraries(policy PRIVATE
  sapi::base
  sandbox2::config
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:print_fds
add_executable(print_fds
  print_fds.cc
)
add_executable(sandbox2::testcase_print_fds ALIAS print_fds)
target_link_libraries(print_fds PRIVATE
  sapi::base
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:sanitizer
add_executable(sanitizer
  sanitizer.cc
)
add_executable(sandbox2::testcase_sanitizer ALIAS sanitizer)
target_link_libraries(sanitizer PRIVATE
  sapi::base
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:sleep
add_executable(sleep
  sleep.cc
)
add_executable(sandbox2::testcase_sleep ALIAS sleep)
target_link_libraries(sleep PRIVATE
  sapi::base
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:symbolize
add_executable(symbolize
  symbolize.cc
)
add_executable(sandbox2::testcase_symbolize ALIAS symbolize)
target_link_libraries(symbolize PRIVATE
  absl::core_headers
  absl::strings
  sandbox2::temp_file
  sapi::base
  sapi::raw_logging
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:starve
add_executable(starve
  starve.cc
)
add_executable(sandbox2::testcase_starve ALIAS starve)
target_link_libraries(starve PRIVATE
  sapi::base
)


# sandboxed_api/sandbox2/testcases:tsync
add_executable(tsync
  tsync.cc
)
add_executable(sandbox2::testcase_tsync ALIAS tsync)
target_link_libraries(tsync PRIVATE
  -Wl,--whole-archive
  gflags::gflags
  -Wl,--no-whole-archive
  glog::glog
  sandbox2::client
  sandbox2::comms
  sapi::base
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:hostname
add_executable(hostname
  hostname.cc
)
add_executable(sandbox2::testcase_hostname ALIAS hostname)
target_link_libraries(hostname PRIVATE
  sapi::base
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:limits
add_executable(limits
  limits.cc
)
add_executable(sandbox2::testcase_limits ALIAS limits)
target_link_libraries(limits PRIVATE
  sapi::base
  ${_sandbox2_fully_static_linkopts}
)

# sandboxed_api/sandbox2/testcases:namespace
add_executable(namespace
  namespace.cc
)
add_executable(sandbox2::testcase_namespace ALIAS namespace)
target_link_libraries(namespace PRIVATE
  sapi::base
  ${_sandbox2_fully_static_linkopts}
)
