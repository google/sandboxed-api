# Copyright 2022 Google LLC
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

cmake_minimum_required(VERSION 3.13..3.22)
project(cares-sapi CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(ARES REQUIRED IMPORTED_TARGET libcares)

add_sapi_library(cares_sapi
  INPUTS "${ARES_INCLUDEDIR}/ares.h"
  FUNCTIONS
    ares_init_options
    ares_dup
    ares_cancel
    ares_set_local_ip4
    ares_set_local_ip6
    ares_set_local_dev
    ares_set_socket_callback
    ares_set_socket_configure_callback
    ares_set_sortlist
    ares_getaddrinfo
    ares_freeaddrinfo
    ares_set_socket_functions
    ares_send
    ares_query
    ares_search
    ares_gethostbyname
    ares_gethostbyname_file
    ares_gethostbyaddr
    ares_getnameinfo
    ares_fds
    ares_getsock
    ares_timeout
    ares_process
    ares_process_fd
    ares_create_query
    ares_mkquery
    ares_expand_name
    ares_expand_string
  LIBRARY cares
  LIBRARY_NAME cares
  NAMESPACE "c_ares_sapi"
)
add_library(sapi_contrib::cares ALIAS cares_sapi)
target_include_directories(cares_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)
