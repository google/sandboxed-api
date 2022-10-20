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

# sandboxed_api/examples/stringop/lib:stringop_params_proto
sapi_protobuf_generate_cpp(
  _sapi_stringop_params_pb_cc _sapi_stringop_params_pb_h
  stringop_params.proto
)
# Object library to avoid having to use -Wl,--whole-archive. This simulates
# Bazel's alwayslink=1.
add_library(sapi_stringop_params_proto OBJECT
  ${_sapi_stringop_params_pb_cc}
  ${_sapi_stringop_params_pb_h}
)
add_library(sapi::stringop_params_proto ALIAS sapi_stringop_params_proto)
target_include_directories(sapi_stringop_params_proto PUBLIC
  ${Protobuf_INCLUDE_DIRS}
)

# sandboxed_api/examples/stringop/lib:stringop
add_library(sapi_stringop STATIC
  stringop.cc
)
add_library(sapi::stringop ALIAS sapi_stringop)
add_dependencies(sapi_stringop
  sapi::stringop_params_proto
)
target_link_libraries(sapi_stringop
  PRIVATE $<TARGET_OBJECTS:sapi_stringop_params_proto>
          sapi::base
          absl::core_headers
          sapi::lenval_core
  PUBLIC protobuf::libprotobuf
)

# sandboxed_api/examples/stringop/lib:stringop-sapi
add_sapi_library(stringop-sapi
  SOURCES sandbox.h
  FUNCTIONS duplicate_string
            reverse_string
            pb_duplicate_string
            pb_reverse_string
            nop
            violate
            get_raw_c_string
  INPUTS stringop.cc
  LIBRARY sapi_stringop
  LIBRARY_NAME Stringop
  NAMESPACE ""
)
add_library(sapi::stringop_sapi ALIAS stringop-sapi)
target_link_libraries(stringop-sapi PRIVATE
  $<TARGET_OBJECTS:sapi_stringop_params_proto>
  sapi::base
)

if(SAPI_BUILD_TESTING)
  # sandboxed_api/examples/stringop:main_stringop
  add_executable(sapi_main_stringop
    main_stringop.cc
  )
  set_target_properties(sapi_main_stringop PROPERTIES OUTPUT_NAME main_stringop)
  target_link_libraries(sapi_main_stringop PRIVATE
    absl::strings
    absl::time
    absl::log
    sapi::sapi
    sapi::status
    sapi::stringop_sapi
    sapi::test_main
    sapi::vars
  )
  gtest_discover_tests_xcompile(sapi_main_stringop)
endif()
