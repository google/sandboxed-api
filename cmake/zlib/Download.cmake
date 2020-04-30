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

# Downloads and unpacks Abseil at configure time

set(workdir "${CMAKE_BINARY_DIR}/zlib-download")

configure_file("${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in"
               "${workdir}/CMakeLists.txt")
execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
                RESULT_VARIABLE error
                WORKING_DIRECTORY "${workdir}")
if(error)
  message(FATAL_ERROR "CMake step for ${PROJECT_NAME} failed: ${error}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build .
                RESULT_VARIABLE error
                WORKING_DIRECTORY "${workdir}")
if(error)
  message(FATAL_ERROR "Build step for ${PROJECT_NAME} failed: ${error}")
endif()

set(_zlib_src "${CMAKE_BINARY_DIR}/zlib-src")

set(ZLIB_INCLUDE_DIRS ${_zlib_src})

add_library(z STATIC
  ${_zlib_src}/adler32.c
  ${_zlib_src}/compress.c
  ${_zlib_src}/crc32.c
  ${_zlib_src}/crc32.h
  ${_zlib_src}/deflate.c
  ${_zlib_src}/deflate.h
  ${_zlib_src}/gzclose.c
  ${_zlib_src}/gzguts.h
  ${_zlib_src}/gzlib.c
  ${_zlib_src}/gzread.c
  ${_zlib_src}/gzwrite.c
  ${_zlib_src}/infback.c
  ${_zlib_src}/inffast.c
  ${_zlib_src}/inffast.h
  ${_zlib_src}/inffixed.h
  ${_zlib_src}/inflate.c
  ${_zlib_src}/inflate.h
  ${_zlib_src}/inftrees.c
  ${_zlib_src}/inftrees.h
  ${_zlib_src}/trees.c
  ${_zlib_src}/trees.h
  ${_zlib_src}/uncompr.c
  ${_zlib_src}/zconf.h
  ${_zlib_src}/zlib.h
  ${_zlib_src}/zutil.c
  ${_zlib_src}/zutil.h
)
add_library(ZLIB::ZLIB ALIAS z)
target_include_directories(z PUBLIC
  ${_zlib_src}
)
target_compile_options(z PRIVATE
  -w
  -Dverbose=-1
)
target_link_libraries(z PRIVATE
  sapi::base
)
