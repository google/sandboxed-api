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

# sandboxed_api/util:file_base
add_library(sapi_util_file_base ${SAPI_LIB_TYPE}
  path.cc
  path.h
)
add_library(sapi::file_base ALIAS sapi_util_file_base)
target_link_libraries(sapi_util_file_base PRIVATE
  absl::strings
  sapi::base
)

# sandboxed_api/util:file_helpers
add_library(sapi_util_file_helpers ${SAPI_LIB_TYPE}
  file_helpers.cc
  file_helpers.h
)
add_library(sapi::file_helpers ALIAS sapi_util_file_helpers)
target_link_libraries(sapi_util_file_helpers PRIVATE
  absl::status
  absl::strings
  sapi::base
)

# sandboxed_api/util:fileops
add_library(sapi_util_fileops ${SAPI_LIB_TYPE}
  fileops.cc
  fileops.h
)
add_library(sapi::fileops ALIAS sapi_util_fileops)
target_link_libraries(sapi_util_fileops PRIVATE
  absl::strings
  sapi::strerror
  sapi::base
)

# sandboxed_api/util:raw_logging
add_library(sapi_util_raw_logging ${SAPI_LIB_TYPE}
  raw_logging.cc
  raw_logging.h
)
add_library(sapi::raw_logging ALIAS sapi_util_raw_logging)
target_link_libraries(sapi_util_raw_logging
  PRIVATE absl::config
          absl::str_format
          absl::strings
          sapi::strerror
          sapi::base
  INTERFACE absl::core_headers
)

# sandboxed_api/util:runfiles
add_library(sapi_util_runfiles ${SAPI_LIB_TYPE}
  runfiles.h
  runfiles_nobazel.cc
)
add_library(sapi::runfiles ALIAS sapi_util_runfiles)
target_link_libraries(sapi_util_runfiles PRIVATE
  absl::str_format
  absl::strings
  sapi::file_base
  sapi::base
  sapi::raw_logging
)

# sandboxed_api/util:status_proto
sapi_protobuf_generate_cpp(_sapi_util_status_pb_cc _sapi_util_status_pb_h
  status.proto
)
add_library(sapi_util_status_proto ${SAPI_LIB_TYPE}
  ${_sapi_util_status_pb_cc}
  ${_sapi_util_status_pb_h}
)
add_library(sapi::status_proto ALIAS sapi_util_status_proto)
target_link_libraries(sapi_util_status_proto
  PRIVATE sapi::base
  PUBLIC protobuf::libprotobuf
)

# sandboxed_api/util:status
add_library(sapi_util_status ${SAPI_LIB_TYPE}
  status.cc
  status.h
  status_macros.h
)
add_library(sapi::status ALIAS sapi_util_status)
target_link_libraries(sapi_util_status
  PRIVATE absl::core_headers
          absl::status
          absl::strings
          sapi::base
          sapi::strerror
          protobuf::libprotobuf
  PUBLIC sapi::status_proto
)

# sandboxed_api/util:strerror
add_library(sapi_util_strerror ${SAPI_LIB_TYPE}
  strerror.cc
  strerror.h
)
add_library(sapi::strerror ALIAS sapi_util_strerror)
target_link_libraries(sapi_util_strerror PRIVATE
  absl::str_format
  sapi::base
)

# sandboxed_api/util:temp_file
add_library(sapi_util_temp_file ${SAPI_LIB_TYPE}
  temp_file.cc
  temp_file.h
)
add_library(sapi::temp_file ALIAS sapi_util_temp_file)
target_link_libraries(sapi_util_temp_file
  PRIVATE absl::strings
          sapi::fileops
          sapi::status
          sapi::strerror
          sapi::base
  PUBLIC absl::status
         absl::statusor
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  # sandboxed_api/util:file_base_test
  add_executable(sapi_file_base_test
    path_test.cc
  )
  set_target_properties(sapi_file_base_test PROPERTIES
    OUTPUT_NAME file_base_test
  )
  target_link_libraries(sapi_file_base_test PRIVATE
    absl::strings
    sapi::file_base
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sapi_file_base_test)

  # sandboxed_api/util:file_helpers_test
  add_executable(sapi_file_helpers_test
    file_helpers_test.cc
  )
  set_target_properties(sapi_file_helpers_test PROPERTIES
    OUTPUT_NAME file_helpers_test
  )
  target_link_libraries(sapi_file_helpers_test PRIVATE
    absl::strings
    sapi::file_helpers
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sapi_file_helpers_test)

  # sandboxed_api/util:fileops_test
  add_executable(sapi_fileops_test
    fileops_test.cc
  )
  set_target_properties(sapi_fileops_test PROPERTIES
    OUTPUT_NAME fileops_test
  )
  target_link_libraries(sapi_fileops_test PRIVATE
    absl::strings
    sapi::file_helpers
    sapi::fileops
    sapi::status_matchers
    sapi::test_main
    sapi::testing
  )
  gtest_discover_tests_xcompile(sapi_fileops_test PROPERTIES
    ENVIRONMENT "TEST_TMPDIR=/tmp"
    ENVIRONMENT "TEST_SRCDIR=${PROJECT_BINARY_DIR}"
  )

  # sandboxed_api/util:status_matchers
  add_library(sapi_util_status_matchers ${SAPI_LIB_TYPE}
    status_matchers.h
  )
  add_library(sapi::status_matchers ALIAS sapi_util_status_matchers)
  target_link_libraries(sapi_util_status_matchers
    PRIVATE absl::optional
            gmock
            gtest
            sapi::base
    PUBLIC absl::status
           sapi::status
  )

  # sandboxed_api/util:status_test
  add_executable(sapi_status_test
    status_test.cc
  )
  set_target_properties(sapi_status_test PROPERTIES OUTPUT_NAME status_test)
  target_link_libraries(sapi_status_test PRIVATE
    absl::status
    absl::type_traits
    sapi::status
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sapi_status_test)

  # sandboxed_api/util:status_macros_test
  add_executable(sapi_status_macros_test
    status_macros_test.cc
  )
  set_target_properties(sapi_status_macros_test PROPERTIES
    OUTPUT_NAME status_macros_test
  )
  target_link_libraries(sapi_status_macros_test PRIVATE
    absl::status
    absl::statusor
    absl::type_traits
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sapi_status_macros_test)

  # sandboxed_api/util:strerror
  add_executable(sapi_strerror_test
    strerror_test.cc
  )
  set_target_properties(sapi_strerror_test PROPERTIES
    OUTPUT_NAME strerror_test
  )
  target_link_libraries(sapi_strerror_test PRIVATE
    absl::strings
    sapi::strerror
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sapi_strerror_test)

  # sandboxed_api/util:temp_file_test
  add_executable(sapi_temp_file_test
    temp_file_test.cc
  )
  set_target_properties(sapi_temp_file_test PROPERTIES
    OUTPUT_NAME temp_file_test
  )
  target_link_libraries(sapi_temp_file_test PRIVATE
    sapi::file_base
    sapi::fileops
    sapi::status_matchers
    sapi::temp_file
    sapi::test_main
    sapi::testing
  )
  gtest_discover_tests_xcompile(sapi_temp_file_test)
endif()
