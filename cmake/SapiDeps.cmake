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

list(APPEND CMAKE_PREFIX_PATH
  "${PROJECT_BINARY_DIR}/Dependencies/Build/gflags"
  "${PROJECT_BINARY_DIR}/Dependencies/Build/glog"
  "${PROJECT_BINARY_DIR}/Dependencies/Build/protobuf"
)

# Build Abseil directly, as recommended upstream
find_path(absl_src_dir
  absl/base/port.h
  HINTS ${ABSL_ROOT_DIR}
  PATHS ${PROJECT_BINARY_DIR}/Dependencies/Source/absl
)
set(_sapi_saved_CMAKE_CXX_STANDARD ${CMAKE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD ${SAPI_CXX_STANDARD})
add_subdirectory(${absl_src_dir}
                 ${PROJECT_BINARY_DIR}/Dependencies/Build/absl
                 EXCLUDE_FROM_ALL)
if(_sapi_saved_CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD "${_sapi_saved_CMAKE_CXX_STANDARD}")
endif()

# Build Googletest directly, as recommended upstream
find_path(googletest_src_dir
  googletest/include/gtest/gtest.h
  HINTS ${GOOGLETEST_ROOT_DIR}
  PATHS ${PROJECT_BINARY_DIR}/Dependencies/Source/googletest
)
set(gtest_force_shared_crt ON CACHE BOOL "")
add_subdirectory(${googletest_src_dir}
                 ${PROJECT_BINARY_DIR}/Dependencies/Build/googletest
                 EXCLUDE_FROM_ALL)

# Prefer to use static libraries
set(_sapi_saved_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
if(WIN32)
  set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
else()
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
endif()

find_package(gflags REQUIRED)
find_package(glog REQUIRED)
find_package(Libcap REQUIRED)
find_package(Libffi REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Protobuf REQUIRED)

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
