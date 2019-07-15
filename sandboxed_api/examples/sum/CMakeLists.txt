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

add_subdirectory(lib)

# sandboxed_api/examples/sum:main_sum
add_executable(sapi_main_sum
  main_sum.cc
)
add_executable(sapi::main_sum ALIAS sapi_main_sum)
target_link_libraries(sapi_main_sum PRIVATE
  absl::memory
  absl::strings
  glog::glog
  sapi::base
  sapi::flags
  sapi::sapi
  sapi::status
  sapi::sum_sapi
  sapi::vars
)
