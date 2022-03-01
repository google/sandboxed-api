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
project(miniz-sapi CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

add_sapi_library(miniz_sapi
  INPUTS "/usr/include/miniz.h" "/usr/include/stdlib.h"
  LIBRARY miniz
  FUNCTIONS
    #miniz_def_alloc_func
    #miniz_def_free_func
    #miniz_def_realloc_func
#   mz_adler32
    #mz_compress
    #mz_compress2
    #mz_compressBound
    #mz_crc32
    #mz_deflate
    #mz_deflateBound
    #mz_deflateEnd
    #mz_deflateInit
    #mz_deflateInit2
    #mz_deflateReset
    #mz_error
    mz_free
#   mz_inflate
#   mz_inflateEnd
#   mz_inflateInit
#   mz_inflateInit2
#   mz_inflateReset
#   mz_uncompress
#   mz_version
#   mz_zip_add_mem_to_archive_file_in_place
#   mz_zip_add_mem_to_archive_file_in_place_v2
#   mz_zip_clear_last_error
#   mz_zip_end
#   mz_zip_extract_archive_file_to_heap
#   mz_zip_extract_archive_file_to_heap_v2
#   mz_zip_get_archive_file_start_offset
#   mz_zip_get_archive_size
#   mz_zip_get_central_dir_size
#   mz_zip_get_cfile
#   mz_zip_get_error_string
#   mz_zip_get_last_error
#   mz_zip_get_mode
#   mz_zip_get_type
#   mz_zip_is_zip64
#   mz_zip_peek_last_error
#   mz_zip_read_archive_data
#   mz_zip_reader_end
#   mz_zip_reader_extract_file_iter_new
#   mz_zip_reader_extract_file_to_callback
#   mz_zip_reader_extract_file_to_cfile
#   mz_zip_reader_extract_file_to_file
#   mz_zip_reader_extract_file_to_heap
#   mz_zip_reader_extract_file_to_mem
#   mz_zip_reader_extract_file_to_mem_no_alloc
#   mz_zip_reader_extract_iter_free
#   mz_zip_reader_extract_iter_new
#   mz_zip_reader_extract_iter_read
#   mz_zip_reader_extract_to_callback
#   mz_zip_reader_extract_to_cfile
#   mz_zip_reader_extract_to_file
#   mz_zip_reader_extract_to_heap
#   mz_zip_reader_extract_to_mem
#   mz_zip_reader_extract_to_mem_no_alloc
#   mz_zip_reader_file_stat
#   mz_zip_reader_get_filename
#   mz_zip_reader_get_num_files
#   mz_zip_reader_init
#   mz_zip_reader_init_cfile
#   mz_zip_reader_init_file
#   mz_zip_reader_init_file_v2
#   mz_zip_reader_init_mem
#   mz_zip_reader_is_file_a_directory
#   mz_zip_reader_is_file_encrypted
#   mz_zip_reader_is_file_supported
#   mz_zip_reader_locate_file
#   mz_zip_reader_locate_file_v2
#   mz_zip_set_last_error
#   mz_zip_validate_archive
#   mz_zip_validate_file
#   mz_zip_validate_file_archive
#   mz_zip_validate_mem_archive
#   mz_zip_writer_add_cfile
#   mz_zip_writer_add_file
#   mz_zip_writer_add_from_zip_reader
#   mz_zip_writer_add_mem
#   mz_zip_writer_add_mem_ex
#   mz_zip_writer_add_mem_ex_v2
#   mz_zip_writer_add_read_buf_callback
#   mz_zip_writer_end
#   mz_zip_writer_finalize_archive
#   mz_zip_writer_finalize_heap_archive
#   mz_zip_writer_init
#   mz_zip_writer_init_cfile
#   mz_zip_writer_init_file
#   mz_zip_writer_init_file_v2
#   mz_zip_writer_init_from_reader
#   mz_zip_writer_init_from_reader_v2
#   mz_zip_writer_init_heap
#   mz_zip_writer_init_heap_v2
#   mz_zip_writer_init_v2
#   mz_zip_zero_struct
#   tdefl_compress
#   tdefl_compress_buffer
    tdefl_compress_mem_to_heap
#   tdefl_compress_mem_to_mem
#   tdefl_compress_mem_to_output
#   tdefl_compressor_alloc
#   tdefl_compressor_free
#   tdefl_create_comp_flags_from_zip_params
#   tdefl_get_adler32
#   tdefl_get_prev_return_status
#   tdefl_init
#   tdefl_write_image_to_png_file_in_memory
#   tdefl_write_image_to_png_file_in_memory_ex
    tinfl_decompress
    tinfl_decompress_mem_to_callback
    tinfl_decompress_mem_to_heap
    tinfl_decompress_mem_to_mem
    tinfl_decompressor_alloc
    tinfl_decompressor_free
  LIBRARY_NAME Miniz
  NAMESPACE "miniz_sapi"
)
add_library(sapi_contrib::miniz ALIAS miniz_sapi)

target_include_directories(miniz_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)

if(SAPI_ENABLE_EXAMPLES)
  add_subdirectory(example)
endif()

if(SAPI_ENABLE_TESTS AND FALSE)
  add_subdirectory(test)
endif()
