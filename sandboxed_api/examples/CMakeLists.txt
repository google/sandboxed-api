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

if((SAPI_BUILD_EXAMPLES OR SAPI_BUILD_TESTING) AND NOT CMAKE_CROSSCOMPILING)
  # TODO(cblichmann): These are depended on by sapi::sapi_test
  add_subdirectory(stringop)
  add_subdirectory(sum)
endif()

if(SAPI_BUILD_EXAMPLES AND SAPI_DOWNLOAD_ZLIB)
  add_subdirectory(zlib)
endif()
