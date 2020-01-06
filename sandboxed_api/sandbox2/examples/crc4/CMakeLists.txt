# Copyright 2020 Google LLC. All Rights Reserved.
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

# sandboxed_api/sandbox2/examples/crc4:crc4sandbox
add_executable(sandbox2_crc4sandbox
  crc4sandbox.cc
)
add_executable(sandbox2::crc4sandbox ALIAS sandbox2_crc4sandbox)
add_dependencies(sandbox2_crc4sandbox
  sandbox2::crc4bin
)
target_link_libraries(sandbox2_crc4sandbox PRIVATE
  absl::memory
  sandbox2::bpf_helper
  sandbox2::comms
  sandbox2::runfiles
  sandbox2::sandbox2
  sapi::base
  sapi::flags
)

# sandboxed_api/sandbox2/examples/crc4:crc4bin
add_executable(crc4bin
  crc4bin.cc
)
add_executable(sandbox2::crc4bin ALIAS crc4bin)
target_link_libraries(crc4bin PRIVATE
  absl::core_headers
  sandbox2::client
  sandbox2::comms
  sandbox2::util
  sapi::base
  sapi::flags
)
