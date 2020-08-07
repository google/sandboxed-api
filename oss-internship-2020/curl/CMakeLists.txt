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

cmake_minimum_required(VERSION 3.16)

project(curl_sandboxed)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add folder containing the non-sandboxed custom curl library
add_subdirectory(custom_curl)

# Setup Sandboxed API
set(SAPI_ROOT "" CACHE PATH "Path to the Sandboxed API source tree")
set(SAPI_ENABLE_EXAMPLES OFF CACHE BOOL "")
set(SAPI_ENABLE_TESTS OFF CACHE BOOL "")
add_subdirectory(
  "${SAPI_ROOT}"
  "${CMAKE_BINARY_DIR}/sandboxed-api-build"
  EXCLUDE_FROM_ALL
)

# Generate SAPI header
add_sapi_library(curl_sapi

  FUNCTIONS curl_easy_cleanup
            curl_easy_duphandle
            curl_easy_escape
            curl_easy_getinfo
            curl_easy_getinfo_ptr
            curl_easy_init
            curl_easy_pause
            curl_easy_perform
            curl_easy_recv
            curl_easy_reset
            curl_easy_send
            curl_easy_setopt
            curl_easy_setopt_ptr
            curl_easy_setopt_long
            curl_easy_setopt_curl_off_t
            curl_easy_strerror
            curl_easy_unescape
            curl_easy_upkeep

  INPUTS "custom_curl/curl/include/curl/curlver.h"
         "custom_curl/curl/include/curl/system.h" 
         "custom_curl/curl/include/curl/curl.h"
         "custom_curl/curl/include/curl/easy.h"
         "custom_curl/custom_curl.h" 

  LIBRARY custom_curl

  LIBRARY_NAME Curl

  NAMESPACE ""
)

# Include generated SAPI header
target_include_directories(curl_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)

# Add examples
add_subdirectory(examples)
