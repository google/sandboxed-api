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

cmake_minimum_required(VERSION 3.10)

project(GDALSandbox CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SAPI_ROOT "" CACHE PATH "Path to the Sandboxed API source tree")
#   cmake .. -G Ninja -DSAPI_ROOT=$HOME/sapi_root

set(SAPI_ENABLE_EXAMPLES OFF CACHE BOOL "")
set(SAPI_ENABLE_TESTS OFF CACHE BOOL "")
add_subdirectory("${SAPI_ROOT}"
                 "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                 # Omit this to have the full Sandboxed API in IDE
                 EXCLUDE_FROM_ALL)

add_library(libgdal STATIC IMPORTED)
set_property(TARGET libgdal PROPERTY IMPORTED_LOCATION 
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/libgdal.a")


# TODO: Build PROJ statically and link it as gdal
# TODO: Use environment variables to specify path prefix to libproj and gdal header
# TODO: Add environment variable to enable tests

target_link_libraries(libgdal INTERFACE
    crypto expat jpeg 
    /usr/local/lib/libproj.so 
    sqlite3 tiff z pthread m rt dl curl)

add_sapi_library(gdal_sapi
    FUNCTIONS 
      GDALOpen 
      GDALAllRegister 
      GDALGetDatasetDriver 
      GDALCreate
      GDALGetDriverByName
      GDALGetRasterBand 
      GDALSetRasterColorInterpretation
      GDALSetProjection 
      GDALSetGeoTransform  
      GDALSetRasterNoDataValue 
      GDALRasterIO 
      GDALClose

    INPUTS "../gdal/gdal/gcore/gdal.h"
    LIBRARY libgdal
    LIBRARY_NAME gdal
    
    NAMESPACE "gdal::sandbox"
)

target_include_directories(gdal_sapi INTERFACE
    "${PROJECT_BINARY_DIR}"
)

add_library(data_retriever STATIC
  get_raster_data.h get_raster_data.cc
)

target_link_libraries(data_retriever
  libgdal
)

add_executable(raster_to_gtiff
    raster_to_gtiff.cc
)

target_link_libraries(raster_to_gtiff
    data_retriever
    gdal_sapi
    sapi::sapi
)

find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

add_executable(tests tests.cc)
target_link_libraries(tests 
  PRIVATE
  gdal_sapi
  data_retriever
  sapi::sapi
  sandbox2::temp_file
  sandbox2::fileops
  ${GTEST_LIBRARIES} 
  ${GTEST_MAIN_LIBRARIES}
)
