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

# Downloads and unpacks libunwind at configure time

set(workdir "${CMAKE_BINARY_DIR}/libffi-download")

configure_file("${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in"
               "${workdir}/CMakeLists.txt")
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

set(_ffi_src "${CMAKE_BINARY_DIR}/libffi-src")

set(libffi_INCLUDE_DIR ${_ffi_src}/libffi/include)

add_library(ffi STATIC
  ${_ffi_src}/configure-cmake-gen/fficonfig.h
  ${_ffi_src}/configure-cmake-gen/include/ffi.h
  ${_ffi_src}/configure-cmake-gen/include/ffitarget.h
  ${_ffi_src}/include/ffi_cfi.h
  ${_ffi_src}/include/ffi_common.h
  ${_ffi_src}/src/closures.c
  ${_ffi_src}/src/debug.c
  ${_ffi_src}/src/java_raw_api.c
  ${_ffi_src}/src/prep_cif.c
  ${_ffi_src}/src/raw_api.c
  ${_ffi_src}/src/types.c
  ${_ffi_src}/src/x86/asmnames.h
  ${_ffi_src}/src/x86/ffi.c
  ${_ffi_src}/src/x86/ffi64.c
  ${_ffi_src}/src/x86/ffiw64.c
  ${_ffi_src}/src/x86/internal.h
  ${_ffi_src}/src/x86/internal64.h
  ${_ffi_src}/src/x86/sysv.S
  ${_ffi_src}/src/x86/unix64.S
  ${_ffi_src}/src/x86/win64.S
)
add_library(libffi::libffi ALIAS ffi)
target_include_directories(ffi PUBLIC
  ${_ffi_src}/configure-cmake-gen
  ${_ffi_src}/configure-cmake-gen/include
  ${_ffi_src}/include
)
target_compile_options(ffi PRIVATE
  -Wno-vla
  -Wno-unused-result
)
target_link_libraries(ffi PRIVATE
  sapi::base
)
