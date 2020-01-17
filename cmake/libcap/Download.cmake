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

# Downloads and unpacks libunwind at configure time

set(workdir "${CMAKE_BINARY_DIR}/libcap-download")

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

set(_cap_src "${CMAKE_BINARY_DIR}/libcap-src")

set(libcap_INCLUDE_DIR ${_cap_src}/libcap/include)

add_custom_command(OUTPUT ${_cap_src}/libcap/cap_names.list.h
  VERBATIM
  # Use the same logic as libcap/Makefile
  COMMAND sed -ne [=[/^#define[ \\t]CAP[_A-Z]\+[ \\t]\+[0-9]\+/{s/^#define \([^ \\t]*\)[ \\t]*\([^ \\t]*\)/\{\"\1\",\2\},/p;}]=]
              ${_cap_src}/libcap/include/uapi/linux/capability.h |
          tr [:upper:] [:lower:] > ${_cap_src}/libcap/cap_names.list.h
)

add_executable(libcap_makenames
  ${_cap_src}/libcap/cap_names.list.h
  ${_cap_src}/libcap/_makenames.c
)
target_include_directories(libcap_makenames PUBLIC
  ${_cap_src}/libcap
  ${_cap_src}/libcap/include
  ${_cap_src}/libcap/include/uapi
)

add_custom_command(OUTPUT ${_cap_src}/libcap/cap_names.h
  COMMAND libcap_makenames > ${_cap_src}/libcap/cap_names.h
)

add_library(cap STATIC
  ${_cap_src}/libcap/cap_alloc.c
  ${_cap_src}/libcap/cap_extint.c
  ${_cap_src}/libcap/cap_file.c
  ${_cap_src}/libcap/cap_flag.c
  ${_cap_src}/libcap/cap_names.h
  ${_cap_src}/libcap/cap_proc.c
  ${_cap_src}/libcap/cap_text.c
  ${_cap_src}/libcap/include/uapi/linux/capability.h
  ${_cap_src}/libcap/libcap.h
)
add_library(libcap::libcap ALIAS cap)
target_include_directories(cap PUBLIC
  ${_cap_src}/libcap
  ${_cap_src}/libcap/include
  ${_cap_src}/libcap/include/uapi
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
