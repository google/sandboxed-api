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
project(libraw-sapi CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBRAW REQUIRED IMPORTED_TARGET libraw)

add_sapi_library(libraw_sapi
  INPUTS "${LIBRAW_INCLUDEDIR}/libraw/libraw.h"
  LIBRARY raw
  FUNCTIONS
    libraw_COLOR
    libraw_adjust_sizes_info_only
    libraw_cameraCount
    libraw_cameraList
    libraw_capabilities
    libraw_close
    libraw_dcraw_clear_mem
    libraw_dcraw_make_mem_image
    libraw_dcraw_make_mem_thumb
    libraw_dcraw_ppm_tiff_writer
    libraw_dcraw_process
    libraw_dcraw_thumb_writer
    libraw_free_image
    libraw_get_cam_mul
    libraw_get_color_maximum
    libraw_get_decoder_info
    libraw_get_iheight
    libraw_get_imgother
    libraw_get_iparams
    libraw_get_iwidth
    libraw_get_lensinfo
    libraw_get_pre_mul
    libraw_get_raw_height
    libraw_get_raw_width
    libraw_get_rgb_cam
    libraw_init
    libraw_open_buffer
    libraw_open_file
    libraw_open_file_ex
    libraw_raw2image
    libraw_recycle
    libraw_recycle_datastream
    libraw_set_bright
    libraw_set_dataerror_handler
    libraw_set_demosaic
    libraw_set_exifparser_handler
    libraw_set_fbdd_noiserd
    libraw_set_gamma
    libraw_set_highlight
    libraw_set_memerror_handler
    libraw_set_no_auto_bright
    libraw_set_output_bps
    libraw_set_output_color
    libraw_set_output_tif
    libraw_set_progress_handler
    libraw_set_user_mul
    libraw_strerror
    libraw_strprogress
    libraw_subtract_black
    libraw_unpack
    libraw_unpack_function_name
    libraw_unpack_thumb
    libraw_version
    libraw_versionNumber
  LIBRARY_NAME libraw 
  NAMESPACE "libraw_sapi"
)
add_library(sapi_contrib::libraw ALIAS libraw_sapi)
target_include_directories(libraw_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)
