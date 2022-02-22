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

# we need to use modified versions of some of the library tools

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/gen_files")
file(COPY "${PROJECT_SOURCE_DIR}/openjpeg/src/bin/jp2/convert.c" DESTINATION "${PROJECT_BINARY_DIR}/gen_files/")
file(COPY "${PROJECT_SOURCE_DIR}/openjpeg/src/bin/jp2/convert.h" DESTINATION "${PROJECT_BINARY_DIR}/gen_files/")
file(COPY "${PROJECT_SOURCE_DIR}/examples/convert.patch" DESTINATION "${PROJECT_BINARY_DIR}/gen_files/")
file(COPY "${PROJECT_SOURCE_DIR}/examples/convert_h.patch" DESTINATION "${PROJECT_BINARY_DIR}/gen_files/")

add_custom_command(
  OUTPUT ${PROJECT_BINARY_DIR}/gen_files/convert.cc ${PROJECT_BINARY_DIR}/gen_files/convert.h
  COMMAND cd ${PROJECT_BINARY_DIR}/gen_files && patch < ${PROJECT_SOURCE_DIR}/examples/convert.patch > /dev/null
  COMMAND cd ${PROJECT_BINARY_DIR}/gen_files && patch < ${PROJECT_SOURCE_DIR}/examples/convert_h.patch > /dev/null
  COMMAND mv ${PROJECT_BINARY_DIR}/gen_files/convert.c ${PROJECT_BINARY_DIR}/gen_files/convert.cc
)

add_library(convert_helper STATIC
  ${PROJECT_BINARY_DIR}/gen_files/convert.cc
  ${PROJECT_BINARY_DIR}/gen_files/convert.h
)

add_executable(decompress_sandboxed
  decompress_example.cc
)

target_link_libraries(decompress_sandboxed PRIVATE
  convert_helper
  openjp2_sapi
  sapi::sapi
)

target_link_libraries(convert_helper PRIVATE
  openjp2_sapi
  sapi::sapi
)
