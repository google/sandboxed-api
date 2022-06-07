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

cmake_minimum_required(VERSION 3.13..3.22)

project(sandboxed_libtiff CXX)
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

FetchContent_Declare(libtiff
  GIT_REPOSITORY https://gitlab.com/libtiff/libtiff
  GIT_TAG        v4.4.0  # 2022-05-20
)
FetchContent_MakeAvailable(libtiff)

add_subdirectory(wrapper)

add_sapi_library(tiff_sapi
  # List of functions that we want to include in the
  # generated sandboxed API class
  FUNCTIONS TIFFOpen
            TIFFClose

            TIFFGetField1
            TIFFGetField2
            TIFFGetField3

            TIFFSetFieldUChar1
            TIFFSetFieldUChar2
            TIFFSetFieldUChar3
            TIFFSetFieldSChar1
            TIFFSetFieldSChar2
            TIFFSetFieldSChar3
            TIFFSetFieldU1
            TIFFSetFieldU2
            TIFFSetFieldU3
            TIFFSetFieldS1
            TIFFSetFieldS2
            TIFFSetFieldS3
            TIFFSetFieldUShort1
            TIFFSetFieldUShort2
            TIFFSetFieldUShort3
            TIFFSetFieldSShort1
            TIFFSetFieldSShort2
            TIFFSetFieldSShort3
            TIFFSetFieldULLong1
            TIFFSetFieldULLong2
            TIFFSetFieldULLong3
            TIFFSetFieldSLLong1
            TIFFSetFieldSLLong2
            TIFFSetFieldSLLong3
            TIFFSetFieldFloat1
            TIFFSetFieldFloat2
            TIFFSetFieldFloat3
            TIFFSetFieldDouble1
            TIFFSetFieldDouble2
            TIFFSetFieldDouble3

            TIFFReadRGBATile
            TIFFReadRGBATileExt
            TIFFReadEncodedTile
            TIFFReadEncodedStrip
            TIFFReadFromUserBuffer

            TIFFTileSize
            TIFFSetDirectory
            TIFFFreeDirectory
            TIFFCreateDirectory

            TIFFForceStrileArrayWriting
            TIFFDeferStrileArrayWriting

            TIFFWriteCheck
            TIFFWriteScanline
            TIFFWriteDirectory
            TIFFWriteEncodedTile
            TIFFWriteEncodedStrip

            TIFFGetStrileOffsetWithErr
            TIFFGetStrileByteCountWithErr

            TIFFCreateEXIFDirectory
            TIFFWriteCustomDirectory

  INPUTS "${libtiff_SOURCE_DIR}/libtiff/tiffio.h"
         wrapper/func.h
                          # Header files or .cc files that should be parsed
  LIBRARY wrapped_tiff    # Library dependency from the add_library() above
  LIBRARY_NAME Tiff       # Name prefix for the generated header. Will be
                          # suffixed with "Api" and "Sandbox" as needed.
  NAMESPACE ""            # Optional C++ namespace to wrap the generated code
)
add_library(sapi_contrib::libtiff ALIAS tiff_sapi)
target_include_directories(tiff_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"  # To find the generated SAPI header
)

if (SAPI_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if (BUILD_TESTING AND SAPI_BUILD_TESTING)
  add_subdirectory(test)
endif()
