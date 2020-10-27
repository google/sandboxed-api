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

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/gen_files")
file(COPY "${PROJECT_SOURCE_DIR}/jsonnet/cmd/jsonnet.cpp" DESTINATION "${PROJECT_BINARY_DIR}/gen_files/")
file(COPY "${PROJECT_SOURCE_DIR}/jsonnet.patch" DESTINATION "${PROJECT_BINARY_DIR}/gen_files/")

add_custom_command(
  OUTPUT ${PROJECT_BINARY_DIR}/gen_files/write_helper.cc
  COMMAND cd ${PROJECT_BINARY_DIR}/gen_files && patch < ${PROJECT_SOURCE_DIR}/jsonnet.patch > /dev/null
  COMMAND mv ${PROJECT_BINARY_DIR}/gen_files/jsonnet.cpp ${PROJECT_BINARY_DIR}/gen_files/write_helper.cc
)

list(APPEND JSONNET_SAPI_HEADERS
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/headers
  ${PROJECT_BINARY_DIR}/gen_files
)

add_library(jsonnet_helper STATIC
  ${PROJECT_SOURCE_DIR}/jsonnet_helper.cc
  ${PROJECT_SOURCE_DIR}/jsonnet_helper.h
  ${PROJECT_SOURCE_DIR}/jsonnet/cmd/utils.h
  ${PROJECT_SOURCE_DIR}/jsonnet/cmd/utils.cpp
  ${PROJECT_BINARY_DIR}/gen_files/write_helper.cc
)

target_include_directories(jsonnet_helper PUBLIC
  ${JSONNET_SAPI_HEADERS}
)

target_link_libraries(jsonnet_helper
  libjsonnet_for_binaries
)

foreach(exe base multiple_files yaml_stream formatter)
  add_executable(jsonnet_${exe}_sandboxed
    jsonnet_${exe}_example.cc
  )

  target_link_libraries(jsonnet_${exe}_sandboxed PRIVATE
    libjsonnet
    jsonnet_helper
    jsonnet_sapi
    sandbox2::file_base
    sandbox2::fileops
    sapi::sapi
  )

  target_include_directories(jsonnet_${exe}_sandboxed PUBLIC
    ${JSONNET_SAPI_HEADERS}
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
