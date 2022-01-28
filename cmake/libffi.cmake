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

FetchContent_Declare(libffi
  URL      https://github.com/libffi/libffi/releases/download/v3.3-rc2/libffi-3.3-rc2.tar.gz
  URL_HASH SHA256=653ffdfc67fbb865f39c7e5df2a071c0beb17206ebfb0a9ecb18a18f63f6b263
)
FetchContent_GetProperties(libffi)
if(NOT libffi_POPULATED)
  FetchContent_Populate(libffi)
  set(libffi_STATUS_FILE "${libffi_SOURCE_DIR}/config.status")
  if(EXISTS "${libffi_STATUS_FILE}")
    file(SHA256 "${libffi_STATUS_FILE}" _sapi_CONFIG_STATUS)
  endif()
  if(NOT _sapi_CONFIG_STATUS STREQUAL "${libffi_CONFIG_STATUS}")
    message("-- Running ./configure for libffi...")
    execute_process(
      COMMAND ./configure --disable-dependency-tracking
                          --disable-builddir
                          --quiet
      WORKING_DIRECTORY "${libffi_SOURCE_DIR}"
      RESULT_VARIABLE _sapi_libffi_config_result
    )
    if(NOT _sapi_libffi_config_result EQUAL "0")
      message(FATAL_ERROR "Configuration of libffi dependency failed")
    endif()
    file(SHA256 "${libffi_SOURCE_DIR}/config.status" _sapi_CONFIG_STATUS)
    set(libffi_CONFIG_STATUS "${_sapi_CONFIG_STATUS}" CACHE INTERNAL "")
  endif()
endif()

set(libffi_INCLUDE_DIR ${libffi_SOURCE_DIR}/libffi/include)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  list(APPEND _ffi_platform_srcs
    ${libffi_SOURCE_DIR}/src/x86/asmnames.h
    ${libffi_SOURCE_DIR}/src/x86/ffi.c
    ${libffi_SOURCE_DIR}/src/x86/ffi64.c
    ${libffi_SOURCE_DIR}/src/x86/ffiw64.c
    ${libffi_SOURCE_DIR}/src/x86/internal.h
    ${libffi_SOURCE_DIR}/src/x86/internal64.h
    ${libffi_SOURCE_DIR}/src/x86/sysv.S
    ${libffi_SOURCE_DIR}/src/x86/unix64.S
    ${libffi_SOURCE_DIR}/src/x86/win64.S
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64")
  list(APPEND _ffi_platform_srcs
    ${libffi_SOURCE_DIR}/src/powerpc/ffi.c
    ${libffi_SOURCE_DIR}/src/powerpc/ffi_linux64.c
    ${libffi_SOURCE_DIR}/src/powerpc/ffi_sysv.c
    ${libffi_SOURCE_DIR}/src/powerpc/linux64.S
    ${libffi_SOURCE_DIR}/src/powerpc/linux64_closure.S
    ${libffi_SOURCE_DIR}/src/powerpc/ppc_closure.S
    ${libffi_SOURCE_DIR}/src/powerpc/sysv.S
    # Textual headers
    ${libffi_SOURCE_DIR}/src/powerpc/ffi_powerpc.h
    ${libffi_SOURCE_DIR}/src/powerpc/asm.h
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  list(APPEND _ffi_platform_srcs
    ${libffi_SOURCE_DIR}/src/aarch64/ffi.c
    ${libffi_SOURCE_DIR}/src/aarch64/internal.h
    ${libffi_SOURCE_DIR}/src/aarch64/sysv.S
  )
endif()

add_library(ffi STATIC
  ${libffi_SOURCE_DIR}/fficonfig.h
  ${libffi_SOURCE_DIR}/include/ffi.h
  ${libffi_SOURCE_DIR}/include/ffi_cfi.h
  ${libffi_SOURCE_DIR}/include/ffi_common.h
  ${libffi_SOURCE_DIR}/include/ffitarget.h
  ${libffi_SOURCE_DIR}/src/closures.c
  ${libffi_SOURCE_DIR}/src/debug.c
  ${libffi_SOURCE_DIR}/src/java_raw_api.c
  ${libffi_SOURCE_DIR}/src/prep_cif.c
  ${libffi_SOURCE_DIR}/src/raw_api.c
  ${libffi_SOURCE_DIR}/src/types.c
  ${_ffi_platform_srcs}
)
add_library(libffi::libffi ALIAS ffi)
target_include_directories(ffi PUBLIC
  ${libffi_SOURCE_DIR}
  ${libffi_SOURCE_DIR}/include
)
target_compile_options(ffi PRIVATE
  -Wno-vla
  -Wno-unused-result
)
target_link_libraries(ffi PRIVATE
  sapi::base
)
