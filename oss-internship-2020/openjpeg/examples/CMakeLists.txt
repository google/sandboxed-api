# Copyright 2020 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http:#www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# copy of one of the converting functions
# put here to omit the library's complex build logic
add_library(convert_helper STATIC
  convert_helper.h
  convert_helper.cc
)

add_executable(decompress_sandboxed
    decompress_example.cc
)

target_link_libraries(decompress_sandboxed PRIVATE 
    convert_helper
    openjp2_sapi
    sapi::sapi
)

target_link_libraries(convert_helper PRIVATE  
    openjp2_sapi
    sapi::sapi
)
