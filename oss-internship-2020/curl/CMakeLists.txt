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

cmake_minimum_required(VERSION 3.12)

project(libcurl_sandbox)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

option(SAPI_CURL_ENABLE_EXAMPLES "" ON)
option(SAPI_CURL_ENABLE_TESTS "" ON)

# Add callbacks used by examples and tests
if (SAPI_CURL_ENABLE_EXAMPLES OR SAPI_CURL_ENABLE_TESTS)
  list(APPEND CURL_SAPI_CALLBACKS
    "${CMAKE_CURRENT_SOURCE_DIR}/callbacks/callbacks.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/callbacks/callbacks.cc"
  )
endif()

# Add folder containing the non-sandboxed custom curl library
add_subdirectory(curl_wrapper)

# Setup Sandboxed API
set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
set(SAPI_BUILD_EXAMPLES ${SAPI_CURL_ENABLE_EXAMPLES} CACHE BOOL "" FORCE)
set(SAPI_BUILD_TESTING ${SAPI_CURL_ENABLE_TESTS} CACHE BOOL "" FORCE)
add_subdirectory(
  "${SAPI_ROOT}"
  "${CMAKE_BINARY_DIR}/sandboxed-api-build"
  EXCLUDE_FROM_ALL
)

# Generate SAPI header
add_sapi_library(curl_sapi
  SOURCES sandbox.h

  # List of all the methods in https://curl.haxx.se/libcurl/c/allfuncs.html
  # Some are added or modified because the original ones are not supported
  # by Sandboxed API (details can be found in curl_wrapper.h)
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
            curl_free
            curl_getdate_sapi
            curl_global_cleanup
            curl_global_init
            curl_global_init_mem
            curl_global_sslset
            curl_mime_addpart
            curl_mime_data
            curl_mime_data_cb
            curl_mime_encoder
            curl_mime_filedata
            curl_mime_filename
            curl_mime_free
            curl_mime_headers
            curl_mime_init
            curl_mime_name
            curl_mime_subparts
            curl_mime_type
            curl_multi_add_handle
            curl_multi_assign
            curl_multi_cleanup
            curl_multi_fdset_sapi
            curl_multi_info_read
            curl_multi_init
            curl_multi_perform
            curl_multi_remove_handle
            curl_multi_setopt
            curl_multi_setopt_ptr
            curl_multi_setopt_long
            curl_multi_setopt_curl_off_t
            curl_multi_socket_action
            curl_multi_strerror
            curl_multi_timeout
            curl_multi_poll_sapi
            curl_multi_wait_sapi
            curl_multi_wakeup
            curl_share_init
            curl_share_setopt
            curl_share_setopt_ptr
            curl_share_setopt_long
            curl_share_strerror
            curl_slist_append
            curl_slist_free_all
            curl_url
            curl_url_cleanup
            curl_url_dup
            curl_url_get
            curl_url_set
            curl_version
            curl_version_info

  INPUTS curl_wrapper/curl/include/curl/curl.h
         curl_wrapper/curl_wrapper.h

  LIBRARY curl_wrapper_and_callbacks

  LIBRARY_NAME Curl

  NAMESPACE curl
)
target_include_directories(curl_sapi INTERFACE
  "${PROJECT_BINARY_DIR}" # Include generated SAPI header
)

add_library(curl_util STATIC
  curl_util.cc
  curl_util.h
)
target_link_libraries(curl_util PUBLIC
  curl_sapi
)

# Add examples
if (SAPI_CURL_ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()

# Add tests
if (SAPI_CURL_ENABLE_TESTS)
  add_subdirectory(tests)
endif()
