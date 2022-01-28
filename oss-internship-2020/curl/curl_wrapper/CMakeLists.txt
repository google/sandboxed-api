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

# Wrapper library including curl, wrappers for some methods and callbacks
# The CURL_SAPI_CALLBACKS variable should contain the absolute paths of
# all the files implementing the callbacks
add_library(curl_wrapper_and_callbacks OBJECT
  curl_wrapper.h
  curl_wrapper.cc
  "${CURL_SAPI_CALLBACKS}"
)
set_target_properties(curl_wrapper_and_callbacks
  PROPERTIES LINKER_LANGUAGE C
)

# Flags needed to build curl statically
set(CURL_HIDDEN_SYMBOLS OFF)
set(BUILD_SHARED_LIBS OFF)

# Link the wrapper to the original curl library (testing is disabled in curl)
set(BUILD_TESTING OFF)
add_subdirectory(curl)
target_link_libraries(curl_wrapper_and_callbacks
  CURL::libcurl
  sapi::sapi
)
