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

project(sandboxed_libtiff CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set this on the command-line
set(SAPI_ROOT "" CACHE PATH "Path to the Sandboxed API source tree")
# To obtain a full SAPI_ROOT check out its source separately:
#   git clone https://github.com/google/sandboxed-api.git $HOME/sapi_root
# Then configure:
#   mkdir -p build && cd build
#   cmake .. -G Ninja -DSAPI_ROOT=$HOME/sapi_root

option(TIFF_SAPI_ENABLE_EXAMPLES "" ON)
option(TIFF_SAPI_ENABLE_TESTS "" ON)

set(SAPI_ENABLE_EXAMPLES ${TIFF_SAPI_ENABLE_EXAMPLES} CACHE BOOL "" FORCE)
set(SAPI_ENABLE_TESTS ${TIFF_SAPI_ENABLE_TESTS} CACHE BOOL "" FORCE)

add_subdirectory(wrapper)

add_subdirectory(
    "${SAPI_ROOT}"
    "${CMAKE_BINARY_DIR}/sandboxed-api-build"
    # Omit this to have the full Sandboxed API in IDE
    EXCLUDE_FROM_ALL
)

add_sapi_library(tiff_sapi
    FUNCTIONS   TIFFOpen
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
                            # List of functions that we want to include in the
                            # generated sandboxed API class
    INPUTS wrapper/libtiff/libtiff/tiffio.h
           wrapper/func.h
                            # Header files or .cc files that should be parsed
    LIBRARY wrapped_tiff    # Library dependency from the add_library() above
    LIBRARY_NAME Tiff       # Name prefix for the generated header. Will be
                            # suffixed with "Api" and "Sandbox" as needed.
    NAMESPACE ""            # Optional C++ namespace to wrap the generated code
)

target_include_directories(tiff_sapi INTERFACE
    "${PROJECT_BINARY_DIR}"  # To find the generated SAPI header
)

if (TIFF_SAPI_ENABLE_EXAMPLES)
  add_subdirectory(example)
endif()

if (TIFF_SAPI_ENABLE_TESTS)
  add_subdirectory(test)
endif()
