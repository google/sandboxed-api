# Copyright 2019 Google LLC
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

FetchContent_Declare(gflags
  GIT_REPOSITORY https://github.com/gflags/gflags.git
  GIT_TAG        addd749114fab4f24b7ea1e0f2f837584389e52c # 2020-03-18
)
set(GFLAGS_IS_SUBPROJECT TRUE)
set(GFLAGS_BUILD_SHARED_LIBS ${SAPI_ENABLE_SHARED_LIBS})
set(GFLAGS_INSTALL_SHARED_LIBS ${SAPI_ENABLE_SHARED_LIBS})
set(GFLAGS_INSTALL_HEADERS OFF)
set(GFLAGS_BUILD_TESTING FALSE)

FetchContent_MakeAvailable(gflags)
