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

project(pffft CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_library(pffft STATIC 
    pffft.c 
    pffft.h 
    fftpack.c 
    fftpack.h
)

add_executable(pffft_main
    test_pffft.c
)

target_link_libraries(pffft_main PRIVATE 
    pffft
)

set(MATH_LIBS "")
include(CheckLibraryExists)
check_library_exists(m sin "" LIBM)
if(LIBM)
    list(APPEND MATH_LIBS "m")
endif()

target_link_libraries(pffft PUBLIC ${MATH_LIBS})


# Adding dependencies
set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
# Then configure:
#   mkdir -p build && cd build
#   cmake .. -G Ninja -DSAPI_ROOT=$HOME/sapi_root

set(SAPI_ENABLE_EXAMPLES OFF CACHE BOOL "")
set(SAPI_ENABLE_TESTS OFF CACHE BOOL "")
add_subdirectory("${SAPI_ROOT}"
                 "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                 # Omit this to have the full Sandboxed API in IDE
                 EXCLUDE_FROM_ALL)

add_sapi_library(pffft_sapi
    FUNCTIONS pffft_new_setup
              pffft_destroy_setup
              pffft_transform
              pffft_transform_ordered
              pffft_zreorder
              pffft_zconvolve_accumulate
              pffft_aligned_malloc
              pffft_aligned_free
              pffft_simd_size
              cffti
              cfftf
              cfftb 
              rffti 
              rfftf 
              rfftb 
              cosqi 
              cosqf 
              cosqb 
              costi 
              cost 
              sinqi 
              sinqb 
              sinqf 
              sinti 
              sint

    INPUTS pffft.h fftpack.h
    LIBRARY pffft
    LIBRARY_NAME pffft

    NAMESPACE ""
)

target_include_directories(pffft_sapi INTERFACE
    "${PROJECT_BINARY_DIR}"
)

add_executable(pffft_sandboxed
    main_pffft_sandboxed.cc
)

target_link_libraries(pffft_sandboxed PRIVATE  
    pffft_sapi
    sapi::sapi
)