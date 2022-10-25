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

cmake_minimum_required(VERSION 3.13..2.22)

project(sapi_zip CXX)
include(CTest)
include(GoogleTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

set(BUILD_SHARED_LIBS OFF)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(LIBZIP_DO_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(libzip
  GIT_REPOSITORY https://github.com/nih-at/libzip/
  GIT_TAG        e076a6fd97663025785a64cc8160aa633220be61 # 2022-06-08
)
FetchContent_MakeAvailable(libzip)

add_subdirectory(wrapper)

add_sapi_library(sapi_zip
  FUNCTIONS zip_open_from_source
            zip_close

            zip_get_name
            zip_get_num_entries

            zip_stat
            zip_stat_index

            zip_fopen
            zip_fopen_index
            zip_fclose

            zip_fread
            zip_fseek
            zip_ftell

            zip_source_buffer
            zip_read_fd_to_source
            zip_source_to_fd
            zip_source_filefd
            zip_source_filefd_create
            zip_source_keep
            zip_source_free

            zip_file_add
            zip_file_replace
            zip_delete

            zip_strerror
  INPUTS "${libzip_BINARY_DIR}/zipconf.h"
         "${libzip_SOURCE_DIR}/lib/zip.h"
         wrapper/wrapper_zip.h

  LIBRARY libzip::zip
  LIBRARY_NAME Zip
  NAMESPACE ""
)
add_library(sapi_contrib::libzip ALIAS sapi_zip)
target_include_directories(sapi_zip PUBLIC
  "${PROJECT_BINARY_DIR}"
  "${libzip_BINARY_DIR}"
)
target_sources(sapi_zip PUBLIC
  sandboxed.h
  utils/utils_zip.cc
  utils/utils_zip.h
)
target_link_libraries(sapi_zip PRIVATE
  absl::die_if_null
)

if(SAPI_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_subdirectory(test)
endif()
