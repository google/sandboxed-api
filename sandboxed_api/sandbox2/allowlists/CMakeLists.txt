# Copyright 2025 Google LLC
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

# Description: Collection of allowlist tokens that are used to visibility
# restrict features in Sandbox2.

# sandboxed_api/sandbox2/allowlists:namespaces
add_library(sandbox2_allowlists_namespaces ${SAPI_LIB_TYPE}
  namespaces.h
)
add_library(sandbox2::allowlists_namespaces ALIAS sandbox2_allowlists_namespaces)
target_link_libraries(sandbox2_allowlists_namespaces PRIVATE
  sapi::base
)
