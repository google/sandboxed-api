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

project(libarchive_sapi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED 17)

# Build SAPI library
set(SAPI_ROOT "/usr/local/google/home/amedar/internship/sandboxed-api" CACHE PATH "Path to the Sandboxed API source tree")

add_subdirectory("${SAPI_ROOT}"
  "${CMAKE_BINARY_DIR}/sandboxed-api-build"
  EXCLUDE_FROM_ALL
)

file(STRINGS functions_to_sandbox.txt FUNCTIONS_LIST)

add_sapi_library(
  libarchive_sapi

  #FUNCTIONS ${FUNCTIONS_LIST}
  #FUNCTIONS archive_read_new
  FUNCTIONS
    archive_entry_free
archive_entry_new
archive_entry_pathname
archive_entry_sourcepath
archive_error_string
archive_read_close
archive_read_data_block
archive_read_disk_descend
archive_read_disk_new
archive_read_disk_open
archive_read_disk_set_standard_lookup
archive_read_free
archive_read_new
archive_read_next_header
archive_read_next_header2
archive_read_open_filename
archive_read_support_filter_bzip2
archive_read_support_filter_compress
archive_read_support_filter_gzip
archive_read_support_format_cpio
archive_read_support_format_tar
archive_write_add_filter_bzip2
archive_write_add_filter_compress
archive_write_add_filter_gzip
archive_write_add_filter_none
archive_write_close
archive_write_data
archive_write_data_block
archive_write_disk_new
archive_write_disk_set_options
archive_write_disk_set_standard_lookup
archive_write_free
archive_write_header
archive_write_new
archive_write_open_filename
archive_write_set_format_ustar

  INPUTS libarchive/libarchive/archive.h
  LIBRARY archive
  LIBRARY_NAME Libarchive
  NAMESPACE ""
)

target_include_directories(libarchive_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"  # To find the generated SAPI header
)


add_subdirectory(libarchive)
add_subdirectory(examples)