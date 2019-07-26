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

# Prefer to use static libraries
set(_sapi_saved_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
if(WIN32)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
else()
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
endif()

if(SAPI_ENABLE_TESTS)
  if(SAPI_USE_GOOGLETEST)
    include(cmake/googletest/Download.cmake)
  endif()
  check_target(gtest)
  check_target(gtest_main)
  check_target(gmock)
endif()

if(SAPI_USE_ABSL)
  include(cmake/abseil/Download.cmake)
endif()
check_target(absl::core_headers)

if(SAPI_USE_LIBUNWIND)
  include(cmake/libunwind/Download.cmake)
endif()
check_target(unwind_ptrace)
check_target(unwind_ptrace_wrapped)

if(SAPI_USE_GFLAGS)
  include(cmake/gflags/Download.cmake)
endif()
check_target(gflags)

if(SAPI_USE_GLOG)
  include(cmake/glog/Download.cmake)
endif()
check_target(glog::glog)

if(SAPI_USE_PROTOBUF)
  include(cmake/protobuf/Download.cmake)
endif()
check_target(protobuf::libprotobuf)
find_package(Protobuf REQUIRED)

find_package(Libcap REQUIRED)
find_package(Libffi REQUIRED)
if(SAPI_ENABLE_EXAMPLES)
  find_package(ZLIB REQUIRED)
endif()

if(CMAKE_VERSION VERSION_LESS "3.12")
  # Work around FindPythonInterp sometimes not preferring Python 3.
  foreach(v IN ITEMS 3 3.9 3.8 3.7 3.6 3.5 3.4 3.3 3.2 3.1 3.0)
    list(APPEND _sapi_py_names python${v})
  endforeach()
  find_program(Python3_EXECUTABLE NAMES ${_sapi_py_names})
  if(NOT Python3_EXECUTABLE)
    message(FATAL_ERROR "No suitable version of Python 3 found")
  endif()
else()
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()

# Undo global change
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_sapi_saved_CMAKE_FIND_LIBRARY_SUFFIXES})
