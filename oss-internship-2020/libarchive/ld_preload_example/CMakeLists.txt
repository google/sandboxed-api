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

add_library(minitar_lib SHARED
  minitar.cc
  minitar.h
)

target_link_libraries(minitar_lib PRIVATE
  archive
)

add_executable(minitar_original
  minitar_main.cc
)

target_link_libraries(minitar_original PRIVATE
    minitar_lib
)

add_library(sapi_minitar_lib_shared SHARED
  ../examples/sapi_minitar.cc
  ../examples/sapi_minitar.h
  ../examples/sandbox.h
)

target_link_libraries(sapi_minitar_lib_shared PUBLIC
  absl::log
  libarchive_sapi
  sandbox2::executor
  sapi::fileops
  sapi::file_base
  sandbox2::util
  sapi::temp_file
  sapi::sapi
)

target_include_directories(sapi_minitar_lib_shared INTERFACE
  "${PROJECT_SOURCE_DIR}/examples"
)

add_library(minitar_preload SHARED
  sapi_minitar.cc
)

target_link_libraries(minitar_preload PRIVATE
  sapi_minitar_lib_shared
)
