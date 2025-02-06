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

# sandboxed_api/sandbox2/allowlists:all_syscalls
add_library(sandbox2_allowlists_all_syscalls ${SAPI_LIB_TYPE}
  all_syscalls.h
)
add_library(sandbox2::allowlists_all_syscalls ALIAS sandbox2_allowlists_all_syscalls)
target_link_libraries(sandbox2_allowlists_all_syscalls PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2/allowlists:namespaces
add_library(sandbox2_allowlists_namespaces ${SAPI_LIB_TYPE}
  namespaces.h
)
add_library(sandbox2::allowlists_namespaces ALIAS
  sandbox2_allowlists_namespaces)
target_link_libraries(sandbox2_allowlists_namespaces PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2allowlists:map_exec
add_library(sandbox2_allowlists_map_exec ${SAPI_LIB_TYPE}
  map_exec.h
)
add_library(sandbox2::allowlists_map_exec ALIAS
  sandbox2_allowlists_map_exec)
target_link_libraries(sandbox2_allowlists_map_exec PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2/allowlists:seccomp_speculation
add_library(sandbox2_allowlists_seccomp_speculation ${SAPI_LIB_TYPE}
  seccomp_speculation.h
)
add_library(sandbox2::allowlists_seccomp_speculation ALIAS
  sandbox2_allowlists_seccomp_speculation)
target_link_libraries(sandbox2_allowlists_seccomp_speculation PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2/allowlists:unrestricted_networking
add_library(sandbox2_allowlists_unrestricted_networking ${SAPI_LIB_TYPE}
  unrestricted_networking.h
)
add_library(sandbox2::allowlists_unrestricted_networking ALIAS
  sandbox2_allowlists_unrestricted_networking)
target_link_libraries(sandbox2_allowlists_unrestricted_networking PRIVATE
  sapi::base
)

# sandboxed_api/sandbox2/allowlists:trace_all_syscalls
add_library(sandbox2_allowlists_trace_all_syscalls ${SAPI_LIB_TYPE}
  trace_all_syscalls.h
)
add_library(sandbox2::allowlists_trace_all_syscalls ALIAS
  sandbox2_allowlists_trace_all_syscalls)
target_link_libraries(sandbox2_allowlists_trace_all_syscalls PRIVATE
  sapi::base
)
