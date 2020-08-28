# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_custom_target(patching_reverse_before
  COMMAND cd ${PROJECT_SOURCE_DIR}/openjpeg/src/bin/jp2/ && patch -R --no-backup-if-mismatch -r /dev/null convert.c < ${PROJECT_SOURCE_DIR}/examples/convert.patch > /dev/null || (exit 0)
  COMMAND cd ${PROJECT_SOURCE_DIR}/openjpeg/src/bin/jp2/ && patch -R --no-backup-if-mismatch -r /dev/null convert.h < ${PROJECT_SOURCE_DIR}/examples/convert_h.patch > /dev/null || (exit 0)
  )

add_custom_command(
  DEPENDS opj_decompress opj_compress opj_dump
  OUTPUT ${PROJECT_SOURCE_DIR}/examples/convert_lib.cc ${PROJECT_SOURCE_DIR}/examples/convert_lib.h
  COMMAND cd ${PROJECT_SOURCE_DIR}/openjpeg/src/bin/jp2/ && patch convert.c < ${PROJECT_SOURCE_DIR}/examples/convert.patch > /dev/null && cp convert.c ${PROJECT_SOURCE_DIR}/examples/convert_lib.cc
  COMMAND cd ${PROJECT_SOURCE_DIR}/openjpeg/src/bin/jp2/ && patch convert.h < ${PROJECT_SOURCE_DIR}/examples/convert_h.patch > /dev/null && cp convert.h ${PROJECT_SOURCE_DIR}/examples/convert_lib.h
  )

add_library(convert_helper STATIC
  ${PROJECT_SOURCE_DIR}/examples/convert_lib.cc
  ${PROJECT_SOURCE_DIR}/examples/convert_lib.h
)

add_dependencies(opj_decompress patching_reverse_before)
add_dependencies(opj_compress patching_reverse_before)
add_dependencies(opj_dump patching_reverse_before)

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

add_custom_target(patching_reverse ALL
  COMMAND cd ${PROJECT_SOURCE_DIR}/openjpeg/src/bin/jp2/ && patch -R convert.c < ${PROJECT_SOURCE_DIR}/examples/convert.patch > /dev/null
  COMMAND cd ${PROJECT_SOURCE_DIR}/openjpeg/src/bin/jp2/ && patch -R convert.h < ${PROJECT_SOURCE_DIR}/examples/convert_h.patch > /dev/null
  )

add_dependencies(patching_reverse decompress_sandboxed)
