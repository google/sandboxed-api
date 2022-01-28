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

# All the examples are sandboxed versions of curl's examples
# (https://curl.haxx.se/libcurl/c/example.html)

# Example 1: simple.c
add_executable(example1
  example1.cc
  ../sandbox.h
)
target_link_libraries(example1 PRIVATE
  curl_sapi
  curl_util
  sapi::sapi
)

# Example 2: getinmemory.c
add_executable(example2
  example2.cc
  ../sandbox.h
)
target_link_libraries(example2 PRIVATE
  curl_sapi
  curl_util
  sapi::sapi
)

# Example 3: simplessl.c
add_executable(example3
  example3.cc
  ../sandbox.h
)
target_link_libraries(example3 PRIVATE
  curl_sapi
  curl_util
  sapi::sapi
)

# Example 4: multi-poll.c
add_executable(example4
  example4.cc
  ../sandbox.h
)
target_link_libraries(example4 PRIVATE
  curl_sapi
  curl_util
  sapi::sapi
)

# Example 5: multithread.c
add_executable(example5
  example5.cc
  ../sandbox.h
)
target_link_libraries(example5 PRIVATE
  curl_sapi
  curl_util
  sapi::sapi
)

# Example 6: simple.c (using transactions)
add_executable(example6
  example6.cc
  ../sandbox.h
)
target_link_libraries(example6 PRIVATE
  curl_sapi
  curl_util
  sapi::sapi
)
