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

# sandboxed_api/examples/sum/lib:sum_params_proto
sapi_protobuf_generate_cpp(_sapi_sum_params_pb_cc _sapi_sum_params_pb_h
  sum_params.proto
)
# Object library to avoid having to use -Wl,--whole-archive. This simulates
# Bazel's alwayslink=1.
add_library(sapi_sum_params_proto OBJECT
  ${_sapi_sum_params_pb_cc}
  ${_sapi_sum_params_pb_h}
)
add_library(sapi::sum_params_proto ALIAS sapi_sum_params_proto)
target_include_directories(sapi_sum_params_proto PUBLIC
  ${Protobuf_INCLUDE_DIRS}
)

# sandboxed_api/examples/sum/lib:sum
add_library(sapi_sum STATIC
  sum.c
  sum_cpp.cc
)
add_library(sapi::sum ALIAS sapi_sum)
add_dependencies(sapi_sum
  sapi::sum_params_proto
)
target_link_libraries(sapi_sum
  PRIVATE $<TARGET_OBJECTS:sapi_sum_params_proto>
          absl::log
          sapi::base
  PUBLIC protobuf::libprotobuf
)

# sandboxed_api/examples/sum/lib:sum-sapi
add_sapi_library(sum-sapi
  SOURCES sandbox.h
  FUNCTIONS sum
            sums
            addf
            sub
            mul
            divs
            muld
            crash
            violate
            sumarr
            testptr
            read_int
            sleep_for_sec
            sumproto
  INPUTS sum.c
         sum_cpp.cc
  LIBRARY sapi_sum
  LIBRARY_NAME Sum
  NAMESPACE ""
)
add_library(sapi::sum_sapi ALIAS sum-sapi)
target_link_libraries(sum-sapi PRIVATE
  $<TARGET_OBJECTS:sapi_sum_params_proto>
  sapi::base
)

# sandboxed_api/examples/sum:main_sum
add_executable(sapi_main_sum
  main_sum.cc
)
set_target_properties(sapi_main_sum PROPERTIES OUTPUT_NAME main_sum)
add_executable(sapi::main_sum ALIAS sapi_main_sum)
target_link_libraries(sapi_main_sum PRIVATE
  absl::log
  absl::log_initialize
  absl::flags
  absl::flags_parse
  absl::strings
  sapi::base
  sapi::sapi
  sapi::status
  sapi::sum_sapi
  sapi::vars
)
