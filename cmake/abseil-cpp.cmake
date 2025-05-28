# Copyright 2019 Google LLC
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

FetchContent_Declare(absl
  URL https://github.com/abseil/abseil-cpp/archive/e4c43850ad008b362b53622cb3c88fd915d8f714.zip # 2025-05-23
  URL_HASH SHA256=00d20e61e2d5dfe86dee88d70897fcdbe593696dfc8ac162873b5fce718557ae
)
set(ABSL_CXX_STANDARD ${SAPI_CXX_STANDARD} CACHE STRING "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(ABSL_RUN_TESTS OFF CACHE BOOL "" FORCE)
set(ABSL_BUILD_TEST_HELPERS ON CACHE BOOL "" FORCE)
set(ABSL_USE_EXTERNAL_GOOGLETEST ON)
set(ABSL_FIND_GOOGLETEST OFF)
set(ABSL_USE_GOOGLETEST_HEAD OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(absl)
