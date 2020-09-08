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

# small example - todo delete later
add_executable(small_example
    small_example.cc
)

target_link_libraries(small_example PRIVATE
    archive
)

add_executable(sapi_example
    sapi_example.cc
)

target_link_libraries(sapi_example PRIVATE
    libarchive_sapi
    sapi::sapi
    glog::glog
)

add_executable(untar
    untar.cc
)

target_link_libraries(untar PRIVATE
    archive
)



add_executable(tarfilter
    tarfilter.cc
)

target_link_libraries(tarfilter PRIVATE
    archive
)






add_executable(minitar
    minitar.cc
)

target_link_libraries(minitar PRIVATE
    archive
)
