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

cmake_minimum_required(VERSION 3.10)

project(test CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(PNG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(libpcre REQUIRED IMPORTED_TARGET libpcre)
pkg_check_modules(proj REQUIRED IMPORTED_TARGET proj)

set(SAPI_ROOT "${PROJECT_SOURCE_DIR}/../.." CACHE PATH "Path to the Sandboxed API source tree")
#   cmake .. -G Ninja -DSAPI_ROOT=$HOME/sapi_root

set(SAPI_BUILD_EXAMPLES OFF CACHE BOOL "")
set(SAPI_BUILD_TESTING OFF CACHE BOOL "")
add_subdirectory("${SAPI_ROOT}"
                 "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                 # Omit this to have the full Sandboxed API in IDE
                 EXCLUDE_FROM_ALL)

add_library(libgdal STATIC IMPORTED)
set_property(TARGET libgdal PROPERTY IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/lib/libgdal.a")

target_link_libraries(libgdal INTERFACE
  crypto
  expat
  jpeg
  PkgConfig::libpcre
  PkgConfig::proj
  sqlite3
  tiff
  z
  pthread
  m
  rt
  dl
  curl
  PNG::PNG
)

add_sapi_library(gdal_sapi
  FUNCTIONS GDALOpen
            GDALAllRegister
            GDALGetDatasetDriver
            GDALGetDriverShortName
            GDALGetDriverLongName
            GDALGetGeoTransform
            GDALGetRasterBand
            GDALGetBlockSize
            GDALGetRasterBandXSize
            GDALGetRasterBandYSize
            GDALRasterIO
  INPUTS "/usr/include/gdal/gdal.h"
  LIBRARY libgdal
  LIBRARY_NAME GDAL
  NAMESPACE ""
)

target_include_directories(gdal_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)

add_executable(raster
  raster.cc
)

target_link_libraries(raster
  gdal_sapi
  sapi::sapi
)
