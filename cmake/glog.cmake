# Copyright 2019 Google LLC
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

FetchContent_Declare(glog
  GIT_REPOSITORY https://github.com/google/glog.git
  GIT_TAG        3ba8976592274bc1f907c402ce22558011d6fc5e # 2020-02-16
)
# Force gflags from subdirectory
set(WITH_GFLAGS FALSE CACHE BOOL "" FORCE)
set(HAVE_LIB_GFLAGS TRUE CACHE STRING "" FORCE)

set(WITH_UNWIND FALSE CACHE BOOL "" FORCE)
set(UNWIND_LIBRARY FALSE)

set(HAVE_PWD_H FALSE)
set(WITH_PKGCONFIG TRUE CACHE BOOL "" FORCE)

set(BUILD_SHARED_LIBS ${SAPI_ENABLE_SHARED_LIBS})

FetchContent_MakeAvailable(glog)
target_include_directories(glog PUBLIC
  $<BUILD_INTERFACE:${gflags_BINARY_DIR}/include>
  $<BUILD_INTERFACE:${gflags_BINARY_DIR}>
)
add_library(gflags_nothreads STATIC IMPORTED)
set_target_properties(gflags_nothreads PROPERTIES
  IMPORTED_LOCATION "${gflags_BINARY_DIR}/libgflags_nothreads.a"
)
target_link_libraries(glog PRIVATE
  -Wl,--whole-archive gflags_nothreads -Wl,--no-whole-archive
)
