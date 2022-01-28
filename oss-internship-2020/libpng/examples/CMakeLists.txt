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

add_executable(pngtopng
  example1.cc
  ../tests/libpng.h
  ../sandboxed.h
)

find_package(PNG REQUIRED)

target_link_libraries(pngtopng PRIVATE
  sapi::sapi
  sapi::temp_file
  libpng_sapi
  "${PNG_LIBRARY}"
)

target_include_directories(pngtopng INTERFACE
  "${PNG_INCLUDE_DIR}"
)


add_executable(rgbtobgr
  example2.cc
  ../tests/libpng.h
  ../sandboxed.h
)

target_link_libraries(rgbtobgr PRIVATE
  sapi::sapi
  sapi::temp_file
  libpng_sapi
  "${PNG_LIBRARY}"
)

target_include_directories(rgbtobgr INTERFACE
  "${PNG_INCLUDE_DIR}"
)

