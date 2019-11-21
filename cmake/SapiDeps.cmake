# Copyright 2019 Google LLC. All Rights Reserved.
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

function(check_target target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR " SAPI: compiling Sandboxed API requires a ${target}
                   CMake target in your project")
  endif()
endfunction()

# Use static libraries
set(_sapi_saved_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_STATIC_LIBRARY_SUFFIX})

if(SAPI_ENABLE_TESTS)
  if(SAPI_DOWNLOAD_GOOGLETEST)
    include(cmake/googletest/Download.cmake)
  endif()
  check_target(gtest)
  check_target(gtest_main)
  check_target(gmock)

  if(SAPI_DOWNLOAD_BENCHMARK)
    include(cmake/benchmark/Download.cmake)
  endif()
  check_target(benchmark)
endif()

if(SAPI_DOWNLOAD_ABSL)
  include(cmake/abseil/Download.cmake)
endif()
check_target(absl::core_headers)

if(SAPI_DOWNLOAD_LIBUNWIND)
  include(cmake/libunwind/Download.cmake)
endif()
check_target(unwind_ptrace)
check_target(unwind_ptrace_wrapped)

if(SAPI_DOWNLOAD_GFLAGS)
  include(cmake/gflags/Download.cmake)
endif()
check_target(gflags)

if(SAPI_DOWNLOAD_GLOG)
  include(cmake/glog/Download.cmake)
endif()
check_target(glog::glog)

if(SAPI_DOWNLOAD_PROTOBUF)
  include(cmake/protobuf/Download.cmake)
  check_target(protobuf::libprotobuf)
  check_target(protobuf::protoc)
else()
  find_package(Protobuf REQUIRED)
endif()

find_package(Libcap REQUIRED)
find_package(Libffi REQUIRED)
if(SAPI_ENABLE_EXAMPLES)
  find_package(ZLIB REQUIRED)
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Undo global change
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_sapi_saved_CMAKE_FIND_LIBRARY_SUFFIXES})
