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

# sandboxed_api/sandbox2/examples/tools:sandbox2tool
add_executable(sandbox2_sandbox2tool
  sandbox2tool.cc
)
set_target_properties(sandbox2_sandbox2tool PROPERTIES OUTPUT_NAME sandbox2tool)
add_executable(sandbox2::sandbox2tool ALIAS sandbox2_sandbox2tool)
target_link_libraries(sandbox2_sandbox2tool PRIVATE
  absl::flags
  absl::flags_parse
  absl::flags_usage
  absl::log
  absl::log_globals
  absl::log_initialize
  absl::strings
  absl::time
  sandbox2::bpf_helper
  sandbox2::sandbox2
  sandbox2::util
  sapi::base
)
