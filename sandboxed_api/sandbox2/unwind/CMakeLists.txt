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

# sandboxed_api/sandbox2/unwind:ptrace_hook
add_library(sandbox2_ptrace_hook STATIC
  ptrace_hook.cc
  ptrace_hook.h
)
add_library(sandbox2::ptrace_hook ALIAS sandbox2_ptrace_hook)
target_link_libraries(sandbox2_ptrace_hook
  PRIVATE sapi::base
          sandbox2::syscall_trap
  PUBLIC absl::strings
)

# sandboxed_api/sandbox2/unwind:unwind
add_library(sandbox2_unwind STATIC
  unwind.cc
  unwind.h
)
add_library(sandbox2::unwind ALIAS sandbox2_unwind)
target_link_libraries(sandbox2_unwind PRIVATE
  absl::cleanup
  absl::statusor
  absl::strings
  sandbox2::comms
  sandbox2::maps_parser
  sandbox2::minielf
  sandbox2::ptrace_hook
  sapi::strerror
  sandbox2::unwind_proto
  sapi::base
  sapi::config
  sapi::file_helpers
  sapi::raw_logging
  sapi::status
  unwind::unwind_ptrace
)

# sandboxed_api/sandbox2/unwind:unwind_proto
sapi_protobuf_generate_cpp(_sandbox2_unwind_pb_h _sandbox2_unwind_pb_cc
  unwind.proto
)
add_library(sandbox2_unwind_proto STATIC
  ${_sandbox2_unwind_pb_cc}
  ${_sandbox2_unwind_pb_h}
)
add_library(sandbox2::unwind_proto ALIAS sandbox2_unwind_proto)
target_link_libraries(sandbox2_unwind_proto PRIVATE
  protobuf::libprotobuf
  sapi::base
)
