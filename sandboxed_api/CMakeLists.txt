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

add_subdirectory(tools/filewrapper)
add_subdirectory(sandbox2)
add_subdirectory(util)
if(SAPI_ENABLE_GENERATOR)
  add_subdirectory(tools/clang_generator)
endif()
add_subdirectory(examples)

# sandboxed_api:proto_arg
sapi_protobuf_generate_cpp(_sapi_proto_arg_pb_cc _sapi_proto_arg_pb_h
  proto_arg.proto
)
add_library(sapi_proto_arg_proto STATIC
  ${_sapi_proto_arg_pb_cc}
  ${_sapi_proto_arg_pb_h}
)
add_library(sapi::proto_arg_proto ALIAS sapi_proto_arg_proto)
target_link_libraries(sapi_proto_arg_proto PRIVATE
  protobuf::libprotobuf
  sapi::base
)

# sandboxed_api:embed_file
add_library(sapi_embed_file STATIC
  embed_file.cc
  embed_file.h
  file_toc.h
)
add_library(sapi::embed_file ALIAS sapi_embed_file)
target_link_libraries(sapi_embed_file PRIVATE
  absl::flat_hash_map
  absl::status
  absl::statusor
  absl::strings
  absl::synchronization
  glog::glog
  sandbox2::fileops
  sandbox2::strerror
  sandbox2::util
  sapi::base
  sapi::raw_logging
  sapi::status
)

# sandboxed_api:sapi
add_library(sapi_sapi STATIC
  sandbox.cc
  sandbox.h
  transaction.cc
  transaction.h
)
add_library(sapi::sapi ALIAS sapi_sapi)
target_link_libraries(sapi_sapi
  PRIVATE absl::flat_hash_map
          absl::memory
          absl::status
          absl::statusor
          absl::str_format
          absl::strings
          absl::synchronization
          sandbox2::bpf_helper
          sandbox2::file_base
          sandbox2::fileops
          sandbox2::runfiles
          sandbox2::sandbox2
          sandbox2::strerror
          sandbox2::util
          sapi::embed_file
          sapi::vars
  PUBLIC absl::core_headers
         sandbox2::client
         sapi::base
         sapi::status
)

# sandboxed_api:call
add_library(sapi_call STATIC
  call.h
)
add_library(sapi::call ALIAS sapi_call)
target_link_libraries(sapi_call PRIVATE
  absl::core_headers
  sapi::var_type
  sapi::base
)

# sandboxed_api:lenval_core
add_library(sapi_lenval_core STATIC
  lenval_core.h
)
add_library(sapi::lenval_core ALIAS sapi_lenval_core)
target_link_libraries(sapi_lenval_core PRIVATE
  sapi::base
)

# sandboxed_api:var_type
add_library(sapi_var_type STATIC
  var_type.h
)
add_library(sapi::var_type ALIAS sapi_var_type)
target_link_libraries(sapi_var_type PRIVATE
  sapi::base
)

# sandboxed_api:vars
add_library(sapi_vars STATIC
  proto_helper.h
  rpcchannel.cc
  rpcchannel.h
  var_abstract.cc
  var_abstract.h
  var_array.h
  var_int.cc
  var_int.h
  var_lenval.cc
  var_lenval.h
  var_pointable.cc
  var_pointable.h
  var_proto.h
  var_ptr.h
  var_reg.h
  var_struct.h
  var_void.h
  vars.h
)
add_library(sapi::vars ALIAS sapi_vars)
target_link_libraries(sapi_vars PRIVATE
  absl::core_headers
  absl::status
  absl::statusor
  absl::str_format
  absl::strings
  absl::synchronization
  glog::glog
  sandbox2::comms
  sapi::base
  sapi::call
  sapi::lenval_core
  sapi::proto_arg_proto
  sapi::status
  sapi::var_type
)

# sandboxed_api:client
add_library(sapi_client STATIC
  client.cc
)
add_library(sapi::client ALIAS sapi_client)
target_link_libraries(sapi_client PRIVATE
  absl::core_headers
  absl::strings
  glog::glog
  libffi::libffi
  sandbox2::client
  sandbox2::comms
  sandbox2::forkingclient
  sandbox2::logsink
  sapi::base
  sapi::call
  sapi::flags
  sapi::lenval_core
  sapi::vars
)

if(SAPI_ENABLE_TESTS)
  # sandboxed_api:sapi_test
  add_executable(sapi_test
    sapi_test.cc
  )
  target_link_libraries(sapi_test PRIVATE
    absl::memory
    absl::status
    benchmark
    sapi::sapi
    sapi::status
    sapi::status_matchers
    sapi::stringop_sapi
    sapi::sum_sapi
    sapi::test_main
  )
  gtest_discover_tests(sapi_test)
endif()
