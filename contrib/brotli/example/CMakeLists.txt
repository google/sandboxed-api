# Copyright 2022 Google LLC
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

add_executable(sapi_minibrotli
  main.cc
  ../utils/utils_brotli.cc
  ../utils/utils_brotli.h
  ../utils/utils_brotli_dec.cc
  ../utils/utils_brotli_dec.h
  ../utils/utils_brotli_enc.cc
  ../utils/utils_brotli_enc.h
)
target_link_libraries(sapi_minibrotli PRIVATE
  absl::die_if_null
  absl::flags
  absl::flags_parse
  absl::log_globals
  absl::log_initialize
  sapi_contrib::brotli
  sapi::sapi
)
