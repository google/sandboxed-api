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
project(libxslt-sapi CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   # Omit this to have the full Sandboxed API in IDE
                   EXCLUDE_FROM_ALL)
endif()

find_package(LibXml2)
find_package(LibXslt)

if(NOT (LIBXML2_FOUND AND LIBXSLT_FOUND))
  message(WARNING "Failed to find libxml2 and libxslt - disabling libxslt example")
  return()
endif()

add_library(sapi_xslt_wrapper wrapper.cc)
target_link_libraries(sapi_xslt_wrapper PUBLIC LibXslt::LibXslt LibXml2::LibXml2)

add_sapi_library(libxslt_sapi
  INPUTS "${PROJECT_SOURCE_DIR}/wrapper.h"
  LIBRARY sapi_xslt_wrapper
  LIBRARY_NAME LibXSLT
  NAMESPACE sapi::contrib::libxslt
  FUNCTIONS
    xmlInitParser
    sapi_xsltParseStylesheetDoc
    xmlReadFd
    xsltApplyStylesheet
    xsltSaveResultToFile
    xsltFreeStylesheet
    xmlFreeDoc
    xmlSaveToFd
    xmlSaveDoc
    xmlSaveFlush
    xmlSaveClose
)
target_include_directories(libxslt_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
  "${SAPI_SOURCE_DIR}"
)

target_link_libraries(libxslt_sapi PUBLIC sapi_xslt_wrapper)

if(SAPI_ENABLE_TESTS)
  add_executable(sapi_libxslt_test sapi_libxslt_test.cc)
  target_link_libraries(sapi_libxslt_test PRIVATE
    libxslt_sapi
    sapi::base
    gtest
    gmock
  )
  gtest_discover_tests(sapi_libxslt_test PROPERTIES ENVIRONMENT "TEST_FILES_DIR=${PROJECT_SOURCE_DIR}/testdata")
endif()
