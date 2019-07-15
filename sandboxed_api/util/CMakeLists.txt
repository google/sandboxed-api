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

# sandboxed_api/util:status_proto
protobuf_generate_cpp(_sapi_util_status_pb_cc _sapi_util_status_pb_h
  status.proto
)
add_library(sapi_util_status_proto STATIC
  ${_sapi_util_status_pb_cc}
  ${_sapi_util_status_pb_h}
)
add_library(sapi::status_proto ALIAS sapi_util_status_proto)
target_link_libraries(sapi_util_status_proto PRIVATE
  protobuf::libprotobuf
)

# sandboxed_api/util:status
add_library(sapi_util_status STATIC
  canonical_errors.cc
  canonical_errors.h
  status.cc
  status.h
  status_internal.h
  status_macros.h
)
add_library(sapi::status ALIAS sapi_util_status)
target_link_libraries(sapi_util_status PRIVATE
  absl::core_headers
  absl::strings
  absl::type_traits
  sapi::base
  sapi::status_proto
)

# sandboxed_api/util:statusor
add_library(sapi_util_statusor STATIC
  statusor.h
)
add_library(sapi::statusor ALIAS sapi_util_statusor)
target_link_libraries(sapi_util_statusor PRIVATE
  absl::base
  absl::core_headers
  absl::variant
  sapi::raw_logging
  sapi::status
)

# sandboxed_api/util:flag
add_library(sapi_util_flags STATIC
  flag.h
)
add_library(sapi::flags ALIAS sapi_util_flags)
target_link_libraries(sapi_util_flags PUBLIC
  gflags
)

# sandboxed_api/util:raw_logging
add_library(sapi_util_raw_logging STATIC
  raw_logging.cc
  raw_logging.h
)
add_library(sapi::raw_logging ALIAS sapi_util_raw_logging)
target_link_libraries(sapi_util_raw_logging PRIVATE
  absl::str_format
  absl::strings
  sandbox2::strerror
  sapi::base
)

if(SAPI_ENABLE_TESTS)
  # sandboxed_api/util:status_matchers
  add_library(sapi_util_status_matchers STATIC
    status_matchers.h
  )
  add_library(sapi::status_matchers ALIAS sapi_util_status_matchers)
  target_link_libraries(sapi_util_status_matchers PRIVATE
    absl::optional
    gmock
    gtest
    sapi::base
    sapi::status
    sapi::statusor
  )

  # sandboxed_api/util:status_test
  add_executable(status_test
    status_test.cc
  )
  target_link_libraries(status_test PRIVATE
    sapi::status_matchers
    sapi::test_main
    absl::type_traits
  )
  gtest_discover_tests(status_test)
endif()
