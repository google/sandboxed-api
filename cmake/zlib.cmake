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

FetchContent_Declare(zlib
  URL      https://mirror.bazel.build/zlib.net/zlib-1.2.11.tar.gz
  URL_HASH SHA256=c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1
  PATCH_COMMAND patch -p1
                < "${SAPI_SOURCE_DIR}/sandboxed_api/bazel/external/zlib.patch"
)
FetchContent_MakeAvailable(zlib)

set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIRS ${zlib_SOURCE_DIR})

add_library(ZLIB::ZLIB ALIAS zlibstatic)
target_include_directories(zlibstatic PUBLIC
  ${ZLIB_INCLUDE_DIRS}
)
