# Copyright 2020 Google LLC
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

configure_file("${jsonnet_SOURCE_DIR}/cmd/jsonnet.cpp"
               "${PROJECT_BINARY_DIR}/gen_files/jsonnet.cpp" COPYONLY)

add_custom_command(
  OUTPUT "${PROJECT_BINARY_DIR}/gen_files/write_helper.cc"
  WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/gen_files"
  COMMAND patch -o write_helper.cc
	    < "${PROJECT_SOURCE_DIR}/jsonnet.patch" > /dev/null
)

list(APPEND JSONNET_SAPI_INCLUDE_DIRS
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_BINARY_DIR}
  ${PROJECT_BINARY_DIR}/gen_files
)

add_library(jsonnet_helper STATIC
  ${PROJECT_SOURCE_DIR}/jsonnet_helper.cc
  ${PROJECT_SOURCE_DIR}/jsonnet_helper.h
  ${jsonnet_SOURCE_DIR}/cmd/utils.h
  ${jsonnet_SOURCE_DIR}/cmd/utils.cpp
  ${PROJECT_BINARY_DIR}/gen_files/write_helper.cc
)

target_include_directories(jsonnet_helper PUBLIC
  ${JSONNET_SAPI_INCLUDE_DIRS}
)

target_link_libraries(jsonnet_helper
  libjsonnet_for_binaries
)

foreach(target IN ITEMS base multiple_files yaml_stream formatter)
  add_executable(jsonnet_${target}_sandboxed
    jsonnet_${target}_example.cc
  )

  target_link_libraries(jsonnet_${target}_sandboxed PRIVATE
    libjsonnet
    jsonnet_helper
    jsonnet_sapi
    sapi::file_base
    sapi::fileops
    sapi::sapi
  )

  target_include_directories(jsonnet_${target}_sandboxed PUBLIC
    ${JSONNET_SAPI_INCLUDE_DIRS}
  )
endforeach()

add_executable(jsonnet_base_transacted
  jsonnet_base_transaction.cc
)

target_link_libraries(jsonnet_base_transacted PRIVATE
  libjsonnet
  jsonnet_helper
  jsonnet_sapi
  sapi::sapi
)
