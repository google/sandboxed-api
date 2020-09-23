// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

cmake_minimum_required(VERSION 3.10) 

project(test CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SAPI_ROOT "${PROJECT_SOURCE_DIR}/../.." CACHE PATH "Path to the Sandboxed API source tree")
#   cmake .. -G Ninja -DSAPI_ROOT=$HOME/sapi_root

set(SAPI_ENABLE_EXAMPLES OFF CACHE BOOL "")
set(SAPI_ENABLE_TESTS OFF CACHE BOOL "")
add_subdirectory("${SAPI_ROOT}"
                 "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                 # Omit this to have the full Sandboxed API in IDE
                 EXCLUDE_FROM_ALL)

add_library(libgdal STATIC IMPORTED)
set_property(TARGET libgdal PROPERTY IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/lib/libgdal.a")
               
find_package(PNG REQUIRED)

target_link_libraries(libgdal INTERFACE
  crypto expat jpeg 
  /usr/lib/x86_64-linux-gnu/libproj.so
  /usr/lib/x86_64-linux-gnu/libpcre.so
  sqlite3 tiff z pthread m rt dl curl
  PNG::PNG)

add_sapi_library(gdal_sapi
  FUNCTIONS GDALOpen GDALAllRegister GDALGetDatasetDriver 
    GDALGetDriverShortName GDALGetDriverLongName GDALGetRasterXSize 
    GDALGetRasterYSize GDALGetRasterCount GDALGetProjectionRef
    GDALOpenEx  GDALGetGeoTransform GDALGetRasterBand GDALGetBlockSize
    GDALGetRasterMinimum GDALGetRasterMaximum GDALGetRasterBandXSize
    GDALGetRasterBandYSize GDALRasterIO
  
  INPUTS "/usr/include/gdal/gdal.h" 
  LIBRARY libgdal
  LIBRARY_NAME gdal
  
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