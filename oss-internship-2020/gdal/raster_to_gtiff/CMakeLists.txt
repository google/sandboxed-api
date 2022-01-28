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

project(GDALSandbox CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(SAPI_ROOT "" CACHE PATH "Path to the Sandboxed API source tree")
set(ENABLE_TESTS OFF CACHE BOOL "Enable GDAL sandbox tests")
set(GDAL_HEADER_PREFIX "/usr/local/include" CACHE PATH "Prefix of the path to gdal.h")
set(LIBGDAL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/lib" CACHE PATH "Prefix of the path to libgdal.a")
set(LIBPROJ_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/lib" CACHE PATH "Prefix of the path to libproj.a")

add_subdirectory("${SAPI_ROOT}"
                 "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                 EXCLUDE_FROM_ALL)

add_library(libgdal STATIC IMPORTED)
set_property(TARGET libgdal PROPERTY IMPORTED_LOCATION
            "${LIBGDAL_PREFIX}/libgdal.a")

add_library(libproj STATIC IMPORTED)
set_property(TARGET libproj PROPERTY IMPORTED_LOCATION
            "${LIBPROJ_PREFIX}/libproj.a")

target_link_libraries(libgdal INTERFACE
    crypto
    expat
    jpeg
    libproj
    sqlite3
    tiff
    z
    pthread
    m
    rt
    dl
    curl
)

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

    INPUTS "${GDAL_HEADER_PREFIX}/gdal.h"
    LIBRARY libgdal
    LIBRARY_NAME Gdal

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

add_library(utils STATIC
  utils.h utils.cc
)

target_link_libraries(utils PUBLIC
  sapi::sapi
  sapi::temp_file
  sapi::fileops
  sandbox2::util
  sapi::file_base
)

add_library(gtiff_converter STATIC
  gtiff_converter.h gtiff_converter.cc
)

target_link_libraries(gtiff_converter PUBLIC
  data_retriever
  gdal_sapi
  sapi::sapi
)

add_executable(raster_to_gtiff
    raster_to_gtiff.cc
)

target_link_libraries(raster_to_gtiff
    data_retriever
    utils
    gtiff_converter
)

if (ENABLE_TESTS)
  include(GoogleTest)
  enable_testing()

  add_executable(tests tests.cc)
  target_link_libraries(tests PRIVATE
    data_retriever
    gtiff_converter
    utils
    gtest
    gtest_main
    sapi::testing
    sapi::file_base
  )

  gtest_discover_tests(tests PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp/"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_SOURCE_DIR}"
  )

endif()
