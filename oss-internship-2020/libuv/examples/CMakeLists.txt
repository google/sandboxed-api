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

add_executable(helloworld
  helloworld.cc
)
target_link_libraries(helloworld
  uv_a
  uv_sapi
  sapi::sapi
)

add_executable(idle-basic
  idle-basic.cc
)
target_link_libraries(idle-basic
  uv_a
  uv_sapi
  sapi::sapi
)

add_executable(uvcat
  uvcat.cc
)
target_link_libraries(uvcat
  uv_a
  uv_sapi
  sapi::sapi
)
