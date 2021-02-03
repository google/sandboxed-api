# Copyright 2019 Google LLC
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

set(workdir "${CMAKE_BINARY_DIR}/_deps/libcap-populate")

set(SAPI_LIBCAP_URL
  https://www.kernel.org/pub/linux/libs/security/linux-privs/libcap2/libcap-2.27.tar.gz
  CACHE STRING "")
set(SAPI_LIBCAP_URL_HASH
  SHA256=260b549c154b07c3cdc16b9ccc93c04633c39f4fb6a4a3b8d1fa5b8a9c3f5fe8
  CACHE STRING "") # 2019-04-16
set(SAPI_LIBCAP_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/libcap-src"
                           CACHE STRING "")
set(SAPI_LIBCAP_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/libcap-build"
                           CACHE STRING "")

file(WRITE "${workdir}/CMakeLists.txt" "\
cmake_minimum_required(VERSION ${CMAKE_VERSION})
project(libcap-populate NONE)
include(ExternalProject)
ExternalProject_Add(libcap
  URL               \"${SAPI_LIBCAP_URL}\"
  URL_HASH          \"${SAPI_LIBCAP_URL_HASH}\"
  SOURCE_DIR        \"${SAPI_LIBCAP_SOURCE_DIR}\"
  BINARY_DIR        \"${SAPI_LIBCAP_BINARY_DIR}\"
  CONFIGURE_COMMAND \"\"
  BUILD_COMMAND     \"\"
  INSTALL_COMMAND   \"\"
  TEST_COMMAND      \"\"
)
")

execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
                RESULT_VARIABLE error
                WORKING_DIRECTORY "${workdir}")
if(error)
  message(FATAL_ERROR "CMake step for ${PROJECT_NAME} failed: ${error}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build .
                RESULT_VARIABLE error
                WORKING_DIRECTORY "${workdir}")
if(error)
  message(FATAL_ERROR "Build step for ${PROJECT_NAME} failed: ${error}")
endif()

set(libcap_INCLUDE_DIR "${SAPI_LIBCAP_SOURCE_DIR}/libcap/include")

add_custom_command(OUTPUT ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_names.list.h
  VERBATIM
  COMMAND # Use the same logic as libcap/Makefile
  sed -ne [=[/^#define[ \\t]CAP[_A-Z]\+[ \\t]\+[0-9]\+/{s/^#define \([^ \\t]*\)[ \\t]*\([^ \\t]*\)/\{\"\1\",\2\},/p;}]=]
      ${SAPI_LIBCAP_SOURCE_DIR}/libcap/include/uapi/linux/capability.h |
  tr [:upper:] [:lower:] > ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_names.list.h
)

if (CMAKE_CROSSCOMPILING AND BUILD_C_COMPILER)
  add_custom_command(OUTPUT ${SAPI_LIBCAP_SOURCE_DIR}/libcap/libcap_makenames
    VERBATIM
    # Use the same logic as libcap/Makefile
    COMMAND ${BUILD_C_COMPILER} ${BUILD_C_FLAGS}
                ${SAPI_LIBCAP_SOURCE_DIR}/libcap/_makenames.c
                -o ${SAPI_LIBCAP_SOURCE_DIR}/libcap/libcap_makenames
    DEPENDS ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_names.list.h
            ${SAPI_LIBCAP_SOURCE_DIR}/libcap/_makenames.c
  )

  add_custom_command(OUTPUT ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_names.h
    COMMAND ${SAPI_LIBCAP_SOURCE_DIR}/libcap/libcap_makenames >
                ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_names.h
    DEPENDS ${SAPI_LIBCAP_SOURCE_DIR}/libcap/libcap_makenames
  )
else()
  add_executable(libcap_makenames
    ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_names.list.h
    ${SAPI_LIBCAP_SOURCE_DIR}/libcap/_makenames.c
  )

  target_include_directories(libcap_makenames PUBLIC
    ${SAPI_LIBCAP_SOURCE_DIR}/libcap
    ${SAPI_LIBCAP_SOURCE_DIR}/libcap/include
    ${SAPI_LIBCAP_SOURCE_DIR}/libcap/include/uapi
  )

  add_custom_command(OUTPUT ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_names.h
    COMMAND libcap_makenames > ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_names.h
  )
endif()

add_library(cap STATIC
  ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_alloc.c
  ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_extint.c
  ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_file.c
  ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_flag.c
  ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_names.h
  ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_proc.c
  ${SAPI_LIBCAP_SOURCE_DIR}/libcap/cap_text.c
  ${SAPI_LIBCAP_SOURCE_DIR}/libcap/include/uapi/linux/capability.h
  ${SAPI_LIBCAP_SOURCE_DIR}/libcap/libcap.h
)
add_library(libcap::libcap ALIAS cap)
target_include_directories(cap PUBLIC
  ${SAPI_LIBCAP_SOURCE_DIR}/libcap
  ${SAPI_LIBCAP_SOURCE_DIR}/libcap/include
  ${SAPI_LIBCAP_SOURCE_DIR}/libcap/include/uapi
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
