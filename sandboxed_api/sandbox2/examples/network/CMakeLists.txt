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

# sandboxed_api/sandbox2/examples/network:network_sandbox
add_executable(sandbox2_network_sandbox
  network_sandbox.cc
)
add_executable(sandbox2::network_sandbox ALIAS sandbox2_network_sandbox)
add_dependencies(sandbox2_network_sandbox
  sandbox2::network_bin
)
target_link_libraries(sandbox2_network_sandbox PRIVATE
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_globals
  absl::log_initialize
  sandbox2::bpf_helper
  sandbox2::comms
  sapi::fileops
  sapi::runfiles
  sandbox2::sandbox2
  sapi::base
)

# sandboxed_api/sandbox2/examples/network_bin:network_bin
add_executable(sandbox2_network_bin
  network_bin.cc
)
set_target_properties(sandbox2_network_bin PROPERTIES OUTPUT_NAME network_bin)
add_executable(sandbox2::network_bin ALIAS sandbox2_network_bin)
target_link_libraries(sandbox2_network_bin PRIVATE
  absl::str_format
  sandbox2::client
  sandbox2::comms
  sapi::fileops
  sapi::base
)
