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

project(pffft CXX C)
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT TARGET sapi::sapi)
  set(SAPI_ROOT "../.." CACHE PATH "Path to the Sandboxed API source tree")
  add_subdirectory("${SAPI_ROOT}"
                   "${CMAKE_BINARY_DIR}/sandboxed-api-build"
                   EXCLUDE_FROM_ALL)
endif()

include(CheckLibraryExists)

FetchContent_Declare(pffft
  GIT_REPOSITORY https://bitbucket.org/jpommier/pffft.git
  GIT_TAG        988259a41d1522047a9420e6265a6ba8289c1654 # 2021-12-02
)
FetchContent_MakeAvailable(pffft)

add_library(pffft STATIC
  "${pffft_SOURCE_DIR}/pffft.c"
  "${pffft_SOURCE_DIR}/pffft.h"
  "${pffft_SOURCE_DIR}/fftpack.c"
  "${pffft_SOURCE_DIR}/fftpack.h"
)

add_executable(pffft_main
  "${pffft_SOURCE_DIR}/test_pffft.c"
)
target_link_libraries(pffft_main PRIVATE
  pffft
)

check_library_exists(m sin "" _sapi_HAVE_LIBM)
if(_sapi_HAVE_LIBM)
  target_link_libraries(pffft PUBLIC
    m
  )
endif()

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

  INPUTS "${pffft_SOURCE_DIR}/pffft.h"
         "${pffft_SOURCE_DIR}/fftpack.h"
  LIBRARY pffft
  LIBRARY_NAME Pffft

  NAMESPACE ""
)
add_library(sapi_contrib::pffft ALIAS pffft_sapi)
target_include_directories(pffft_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
  "${SAPI_SOURCE_DIR}"
)

add_executable(pffft_sandboxed
  main_pffft_sandboxed.cc
)
target_link_libraries(pffft_sandboxed PRIVATE
  absl::flags
  absl::flags_parse
  absl::log
  absl::log_globals
  absl::log_initialize
  sapi_contrib::pffft
  sapi::sapi
)
