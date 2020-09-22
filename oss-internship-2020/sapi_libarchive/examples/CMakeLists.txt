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


add_library(helpers STATIC
    helpers.h
    helpers.cc
)

target_link_libraries(helpers PUBLIC
    sapi::sapi
    sandbox2::fileops
    sandbox2::util
    sandbox2::file_base
    sandbox2::executor
    sandbox2::temp_file
    glog::glog
    libarchive_sapi
)

add_executable(sapi_minitar
    sapi_minitar.cc
)

target_link_libraries(sapi_minitar PRIVATE
    helpers
    libarchive_sapi
    sapi::sapi
    #glog::glog
)

add_executable(minitar
    orig/minitar.cc
)

target_link_libraries(minitar PRIVATE
    archive
)
