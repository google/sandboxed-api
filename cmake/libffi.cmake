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

set(workdir "${CMAKE_BINARY_DIR}/_deps/libffi-populate")

set(SAPI_LIBFFI_URL
  https://github.com/libffi/libffi/releases/download/v3.3-rc2/libffi-3.3-rc2.tar.gz
  CACHE STRING "")
set(SAPI_LIBFFI_URL_HASH
  SHA256=653ffdfc67fbb865f39c7e5df2a071c0beb17206ebfb0a9ecb18a18f63f6b263
  CACHE STRING "") # 2019-11-02
set(SAPI_LIBFFI_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/libffi-src"
                           CACHE STRING "")
set(SAPI_LIBFFI_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/libffi-build"
                           CACHE STRING "")

file(WRITE "${workdir}/CMakeLists.txt" "\
cmake_minimum_required(VERSION ${CMAKE_VERSION})
project(libffi-populate NONE)
include(ExternalProject)
ExternalProject_Add(libffi
  URL               \"${SAPI_LIBFFI_URL}\"
  URL_HASH          \"${SAPI_LIBFFI_URL_HASH}\"
  SOURCE_DIR        \"${SAPI_LIBFFI_SOURCE_DIR}\"
  CONFIGURE_COMMAND ./configure
                    --disable-dependency-tracking
                    --disable-builddir
                    ${SAPI_THIRD_PARTY_CONFIGUREOPTS}
  BUILD_COMMAND     \"\"
  INSTALL_COMMAND   \"\"
  TEST_COMMAND      \"\"
  BUILD_IN_SOURCE TRUE
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

set(libffi_INCLUDE_DIR ${SAPI_LIBFFI_SOURCE_DIR}/libffi/include)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  list(APPEND _ffi_platform_srcs
    ${SAPI_LIBFFI_SOURCE_DIR}/src/x86/asmnames.h
    ${SAPI_LIBFFI_SOURCE_DIR}/src/x86/ffi.c
    ${SAPI_LIBFFI_SOURCE_DIR}/src/x86/ffi64.c
    ${SAPI_LIBFFI_SOURCE_DIR}/src/x86/ffiw64.c
    ${SAPI_LIBFFI_SOURCE_DIR}/src/x86/internal.h
    ${SAPI_LIBFFI_SOURCE_DIR}/src/x86/internal64.h
    ${SAPI_LIBFFI_SOURCE_DIR}/src/x86/sysv.S
    ${SAPI_LIBFFI_SOURCE_DIR}/src/x86/unix64.S
    ${SAPI_LIBFFI_SOURCE_DIR}/src/x86/win64.S
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64")
  list(APPEND _ffi_platform_srcs
    ${SAPI_LIBFFI_SOURCE_DIR}/src/powerpc/ffi.c
    ${SAPI_LIBFFI_SOURCE_DIR}/src/powerpc/ffi_linux64.c
    ${SAPI_LIBFFI_SOURCE_DIR}/src/powerpc/ffi_sysv.c
    ${SAPI_LIBFFI_SOURCE_DIR}/src/powerpc/linux64.S
    ${SAPI_LIBFFI_SOURCE_DIR}/src/powerpc/linux64_closure.S
    ${SAPI_LIBFFI_SOURCE_DIR}/src/powerpc/ppc_closure.S
    ${SAPI_LIBFFI_SOURCE_DIR}/src/powerpc/sysv.S
    # Textual headers
    ${SAPI_LIBFFI_SOURCE_DIR}/src/powerpc/ffi_powerpc.h
    ${SAPI_LIBFFI_SOURCE_DIR}/src/powerpc/asm.h
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  list(APPEND _ffi_platform_srcs
    ${SAPI_LIBFFI_SOURCE_DIR}/src/aarch64/ffi.c
    ${SAPI_LIBFFI_SOURCE_DIR}/src/aarch64/internal.h
    ${SAPI_LIBFFI_SOURCE_DIR}/src/aarch64/sysv.S
  )
endif()

add_library(ffi STATIC
  ${SAPI_LIBFFI_SOURCE_DIR}/fficonfig.h
  ${SAPI_LIBFFI_SOURCE_DIR}/include/ffi.h
  ${SAPI_LIBFFI_SOURCE_DIR}/include/ffi_cfi.h
  ${SAPI_LIBFFI_SOURCE_DIR}/include/ffi_common.h
  ${SAPI_LIBFFI_SOURCE_DIR}/include/ffitarget.h
  ${SAPI_LIBFFI_SOURCE_DIR}/src/closures.c
  ${SAPI_LIBFFI_SOURCE_DIR}/src/debug.c
  ${SAPI_LIBFFI_SOURCE_DIR}/src/java_raw_api.c
  ${SAPI_LIBFFI_SOURCE_DIR}/src/prep_cif.c
  ${SAPI_LIBFFI_SOURCE_DIR}/src/raw_api.c
  ${SAPI_LIBFFI_SOURCE_DIR}/src/types.c
  ${_ffi_platform_srcs}
)
add_library(libffi::libffi ALIAS ffi)
target_include_directories(ffi PUBLIC
  ${SAPI_LIBFFI_SOURCE_DIR}
  ${SAPI_LIBFFI_SOURCE_DIR}/include
)
target_compile_options(ffi PRIVATE
  -Wno-vla
  -Wno-unused-result
)
target_link_libraries(ffi PRIVATE
  sapi::base
)
