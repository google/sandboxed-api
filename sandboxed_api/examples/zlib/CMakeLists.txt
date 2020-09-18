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

# sandboxed_api/examples/zlib:zlib-sapi
add_sapi_library(zlib-sapi
  FUNCTIONS deflateInit_
            deflate
            deflateEnd
  INPUTS ${ZLIB_INCLUDE_DIRS}/zlib.h
  LIBRARY ZLIB::ZLIB
  LIBRARY_NAME Zlib
  NAMESPACE "sapi::zlib"
)
add_library(sapi::zlib_sapi ALIAS zlib-sapi)
target_link_libraries(zlib-sapi PRIVATE
  ZLIB::ZLIB
)

# sandboxed_api/examples/zlib:main_zlib
add_executable(main_zlib
  main_zlib.cc
)
target_link_libraries(main_zlib PRIVATE
  absl::status
  sapi::base
  glog::glog
  sapi::flags
  sapi::sapi
  sapi::status
  sapi::zlib_sapi
)
