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

FetchContent_Declare(zlib
  URL      https://mirror.bazel.build/zlib.net/zlib-1.2.11.tar.gz
  URL_HASH SHA256=c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1
  PATCH_COMMAND patch -p1
                < "${SAPI_SOURCE_DIR}/sandboxed_api/bazel/external/zlib.patch"
)
FetchContent_GetProperties(zlib)
if(NOT zlib_POPULATED)
  FetchContent_Populate(zlib)
endif()

set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIRS ${zlib_SOURCE_DIR})

add_library(z STATIC
  ${zlib_SOURCE_DIR}/adler32.c
  ${zlib_SOURCE_DIR}/compress.c
  ${zlib_SOURCE_DIR}/crc32.c
  ${zlib_SOURCE_DIR}/crc32.h
  ${zlib_SOURCE_DIR}/deflate.c
  ${zlib_SOURCE_DIR}/deflate.h
  ${zlib_SOURCE_DIR}/gzclose.c
  ${zlib_SOURCE_DIR}/gzguts.h
  ${zlib_SOURCE_DIR}/gzlib.c
  ${zlib_SOURCE_DIR}/gzread.c
  ${zlib_SOURCE_DIR}/gzwrite.c
  ${zlib_SOURCE_DIR}/infback.c
  ${zlib_SOURCE_DIR}/inffast.c
  ${zlib_SOURCE_DIR}/inffast.h
  ${zlib_SOURCE_DIR}/inffixed.h
  ${zlib_SOURCE_DIR}/inflate.c
  ${zlib_SOURCE_DIR}/inflate.h
  ${zlib_SOURCE_DIR}/inftrees.c
  ${zlib_SOURCE_DIR}/inftrees.h
  ${zlib_SOURCE_DIR}/trees.c
  ${zlib_SOURCE_DIR}/trees.h
  ${zlib_SOURCE_DIR}/uncompr.c
  ${zlib_SOURCE_DIR}/zconf.h
  ${zlib_SOURCE_DIR}/zlib.h
  ${zlib_SOURCE_DIR}/zutil.c
  ${zlib_SOURCE_DIR}/zutil.h
)
add_library(ZLIB::ZLIB ALIAS z)
target_include_directories(z PUBLIC
  ${zlib_SOURCE_DIR}
)
target_compile_options(z PRIVATE
  -w
  -Dverbose=-1
)
target_link_libraries(z PRIVATE
  sapi::base
)

