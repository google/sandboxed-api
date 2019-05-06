# Copyright 2019 Google LLC. All Rights Reserved.
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

add_subdirectory(bazel)  # For filewrapper
add_subdirectory(sandbox2)
add_subdirectory(util)

# sandboxed_api:embed_file
add_library(sapi_embed_file STATIC
  embed_file.cc
  embed_file.h
  file_toc.h
)
add_library(sapi::embed_file ALIAS sapi_embed_file)
target_link_libraries(sapi_embed_file PRIVATE
  absl::flat_hash_map
  absl::strings
  absl::synchronization
  glog::glog
  sandbox2::fileops
  sandbox2::strerror
  sandbox2::util
  sapi::base
  sapi::raw_logging
  sapi::status
)
