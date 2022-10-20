# Copyright 2020 Google LLC
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

foreach(target IN ITEMS base multiple_files yaml_stream formatter)
  add_executable(jsonnet_${target}_sandboxed
    jsonnet_${target}_example.cc
  )
  target_link_libraries(jsonnet_${target}_sandboxed PRIVATE
    absl::check
    absl::flags
    absl::flags_parse
    absl::log
    absl::log_globals
    absl::log_initialize
    libjsonnet
    sapi_contrib::jsonnet_helper
    sapi_contrib::jsonnet
    sapi::file_base
    sapi::fileops
    sapi::sapi
  )
endforeach()

add_executable(jsonnet_base_transacted
  jsonnet_base_transaction.cc
)
target_link_libraries(jsonnet_base_transacted PRIVATE
  absl::check
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_globals
  absl::log_initialize
  libjsonnet
  sapi_contrib::jsonnet_helper
  sapi_contrib::jsonnet
  sapi::sapi
)
