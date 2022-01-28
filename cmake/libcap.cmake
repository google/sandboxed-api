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

FetchContent_Declare(libcap
  URL      https://www.kernel.org/pub/linux/libs/security/linux-privs/libcap2/libcap-2.27.tar.gz
  URL_HASH SHA256=260b549c154b07c3cdc16b9ccc93c04633c39f4fb6a4a3b8d1fa5b8a9c3f5fe8
)
FetchContent_MakeAvailable(libcap)

set(libcap_INCLUDE_DIR "${libcap_SOURCE_DIR}/libcap/include")

add_custom_command(OUTPUT ${libcap_SOURCE_DIR}/libcap/cap_names.list.h
  VERBATIM
  COMMAND # Use the same logic as libcap/Makefile
  sed -ne [=[/^#define[ \\t]CAP[_A-Z]\+[ \\t]\+[0-9]\+/{s/^#define \([^ \\t]*\)[ \\t]*\([^ \\t]*\)/\{\"\1\",\2\},/p;}]=]
      ${libcap_SOURCE_DIR}/libcap/include/uapi/linux/capability.h |
  tr [:upper:] [:lower:] > ${libcap_SOURCE_DIR}/libcap/cap_names.list.h
)

if (CMAKE_CROSSCOMPILING AND BUILD_C_COMPILER)
  add_custom_command(OUTPUT ${libcap_SOURCE_DIR}/libcap/libcap_makenames
    VERBATIM
    # Use the same logic as libcap/Makefile
    COMMAND ${BUILD_C_COMPILER} ${BUILD_C_FLAGS}
                ${libcap_SOURCE_DIR}/libcap/_makenames.c
                -o ${libcap_SOURCE_DIR}/libcap/libcap_makenames
    DEPENDS ${libcap_SOURCE_DIR}/libcap/cap_names.list.h
            ${libcap_SOURCE_DIR}/libcap/_makenames.c
  )

  add_custom_command(OUTPUT ${libcap_SOURCE_DIR}/libcap/cap_names.h
    COMMAND ${libcap_SOURCE_DIR}/libcap/libcap_makenames >
                ${libcap_SOURCE_DIR}/libcap/cap_names.h
    DEPENDS ${libcap_SOURCE_DIR}/libcap/libcap_makenames
  )
else()
  add_executable(libcap_makenames
    ${libcap_SOURCE_DIR}/libcap/cap_names.list.h
    ${libcap_SOURCE_DIR}/libcap/_makenames.c
  )

  target_include_directories(libcap_makenames PUBLIC
    ${libcap_SOURCE_DIR}/libcap
    ${libcap_SOURCE_DIR}/libcap/include
    ${libcap_SOURCE_DIR}/libcap/include/uapi
  )

  add_custom_command(OUTPUT ${libcap_SOURCE_DIR}/libcap/cap_names.h
    COMMAND libcap_makenames > ${libcap_SOURCE_DIR}/libcap/cap_names.h
  )
endif()

add_library(cap STATIC
  ${libcap_SOURCE_DIR}/libcap/cap_alloc.c
  ${libcap_SOURCE_DIR}/libcap/cap_extint.c
  ${libcap_SOURCE_DIR}/libcap/cap_file.c
  ${libcap_SOURCE_DIR}/libcap/cap_flag.c
  ${libcap_SOURCE_DIR}/libcap/cap_names.h
  ${libcap_SOURCE_DIR}/libcap/cap_proc.c
  ${libcap_SOURCE_DIR}/libcap/cap_text.c
  ${libcap_SOURCE_DIR}/libcap/include/uapi/linux/capability.h
  ${libcap_SOURCE_DIR}/libcap/libcap.h
)
add_library(libcap::libcap ALIAS cap)
target_include_directories(cap PUBLIC
  ${libcap_SOURCE_DIR}/libcap
  ${libcap_SOURCE_DIR}/libcap/include
  ${libcap_SOURCE_DIR}/libcap/include/uapi
)
target_compile_options(cap PRIVATE
  -Wno-tautological-compare
  -Wno-unused-result
)
target_compile_definitions(cap PRIVATE
  # Work around sys/xattr.h not declaring this
  -DXATTR_NAME_CAPS="\"security.capability\""
)
target_link_libraries(cap PRIVATE
  sapi::base
)
