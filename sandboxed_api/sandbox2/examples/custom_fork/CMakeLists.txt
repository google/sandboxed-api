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

# sandboxed_api/sandbox2/examples/custom_fork:custom_fork_sandbox
add_executable(sandbox2_custom_fork_sandbox
  custom_fork_sandbox.cc
)
add_executable(sandbox2::custom_fork_sandbox ALIAS sandbox2_custom_fork_sandbox)
add_dependencies(sandbox2_custom_fork_sandbox
  sandbox2::custom_fork_bin
)
target_link_libraries(sandbox2_custom_fork_sandbox PRIVATE
  absl::core_headers
  absl::memory
  sandbox2::comms
  sandbox2::forkserver
  sandbox2::runfiles
  sandbox2::sandbox2
  sapi::base
  sapi::flags
  sapi::raw_logging
)

# sandboxed_api/sandbox2/examples/custom_fork:custom_fork_bin
add_executable(custom_fork_bin
  custom_fork_bin.cc
)
add_executable(sandbox2::custom_fork_bin ALIAS custom_fork_bin)
target_link_libraries(custom_fork_bin PRIVATE
  sandbox2::comms
  sandbox2::forkingclient
  sandbox2::util
  sapi::base
  sapi::flags
  sapi::raw_logging
)
