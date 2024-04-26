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

add_subdirectory(tools/filewrapper)
add_subdirectory(sandbox2)
add_subdirectory(util)
if(SAPI_ENABLE_CLANG_TOOL AND NOT SAPI_CLANG_TOOL_EXECUTABLE)
  add_subdirectory(tools/clang_generator)
endif()
add_subdirectory(examples)

# sandboxed_api:config
add_library(sapi_config ${SAPI_LIB_TYPE}
  config.cc
  config.h
)
add_library(sapi::config ALIAS sapi_config)
target_link_libraries(sapi_config
  PRIVATE sapi::base
  PUBLIC absl::config
)

# sandboxed_api:embed_file
add_library(sapi_embed_file ${SAPI_LIB_TYPE}
  embed_file.cc
  embed_file.h
  file_toc.h
)
add_library(sapi::embed_file ALIAS sapi_embed_file)
target_link_libraries(sapi_embed_file
  PRIVATE absl::strings
          sandbox2::util
          sapi::base
          sapi::raw_logging
  PUBLIC absl::flat_hash_map
         absl::synchronization
         sapi::fileops
)

# sandboxed_api:sapi
add_library(sapi_sapi ${SAPI_LIB_TYPE}
  sandbox.cc
  sandbox.h
  transaction.cc
  transaction.h
)
add_library(sapi::sapi ALIAS sapi_sapi)
target_link_libraries(sapi_sapi
  PRIVATE absl::base
          absl::dynamic_annotations
          absl::flat_hash_map
          absl::log
          absl::log_globals
          absl::span
          absl::status
          absl::statusor
          absl::str_format
          absl::strings
          absl::synchronization
          sapi::call
          sapi::file_base
          sapi::runfiles
          sapi::strerror
          sandbox2::util
          sapi::embed_file
          sapi::var_type
          sapi::vars
  PUBLIC absl::check
         absl::core_headers
         sandbox2::client
         sandbox2::sandbox2
         sapi::base
         sapi::status
)

# sandboxed_api:call
add_library(sapi_call ${SAPI_LIB_TYPE}
  call.h
)
add_library(sapi::call ALIAS sapi_call)
target_link_libraries(sapi_call PRIVATE
  absl::core_headers
  sapi::var_type
  sapi::base
)

# sandboxed_api:lenval_core
add_library(sapi_lenval_core ${SAPI_LIB_TYPE}
  lenval_core.h
)
add_library(sapi::lenval_core ALIAS sapi_lenval_core)
target_link_libraries(sapi_lenval_core PRIVATE
  sapi::base
)

# sandboxed_api:var_type
add_library(sapi_var_type ${SAPI_LIB_TYPE}
  var_type.h
)
add_library(sapi::var_type ALIAS sapi_var_type)
target_link_libraries(sapi_var_type PRIVATE
  sapi::base
)

# sandboxed_api:vars
add_library(sapi_vars ${SAPI_LIB_TYPE}
  rpcchannel.cc
  rpcchannel.h
  var_abstract.cc
  var_abstract.h
  var_array.h
  var_int.cc
  var_int.h
  var_lenval.cc
  var_lenval.h
  var_proto.h
  var_ptr.h
  var_reg.h
  var_struct.h
  var_void.h
  vars.h
)
add_library(sapi::vars ALIAS sapi_vars)
target_link_libraries(sapi_vars
  PRIVATE absl::core_headers
          absl::span
          absl::status
          absl::statusor
          absl::str_format
          absl::strings
          absl::synchronization
          absl::utility
          sandbox2::comms
          sandbox2::util
          sapi::base
          sapi::call
          sapi::lenval_core
          sapi::status
          sapi::var_type
  PUBLIC absl::log
         sapi::proto_helper
)

# sandboxed_api:client
add_library(sapi_client ${SAPI_LIB_TYPE}
  client.cc
)
add_library(sapi::client ALIAS sapi_client)
target_link_libraries(sapi_client
  PRIVATE absl::check
          absl::core_headers
          absl::dynamic_annotations
          absl::flags_parse
          absl::log
          absl::log_flags
          absl::log_initialize
          absl::statusor
          absl::strings
          libffi::libffi
          sandbox2::comms
          sandbox2::forkingclient
          sandbox2::logsink
          sapi::base
          sapi::call
          sapi::lenval_core
          sapi::proto_arg_proto
          sapi::proto_helper
          ${CMAKE_DL_LIBS}
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  # sandboxed_api:embed_file_test
  add_executable(embed_file_test
    embed_file_test.cc
  )
  target_link_libraries(embed_file_test PRIVATE
    absl::memory
    absl::strings
    sapi::embed_file
    sapi::test_main
  )

  # sandboxed_api:testing
  add_library(sapi_testing ${SAPI_LIB_TYPE}
    testing.cc
    testing.h
  )
  add_library(sapi::testing ALIAS sapi_testing)
  target_link_libraries(sapi_testing
   PRIVATE absl::strings
           sapi::file_base
           sapi::base
   PUBLIC sapi::config
          sandbox2::allow_all_syscalls
          sandbox2::policybuilder
  )

  # sandboxed_api:sapi_test
  add_executable(sapi_test
    sapi_test.cc
  )
  target_link_libraries(sapi_test PRIVATE
    absl::status
    absl::statusor
    absl::time
    benchmark
    sandbox2::result
    sapi::proto_arg_proto
    sapi::sapi
    sapi::status
    sapi::status_matchers
    sapi::stringop_sapi
    sapi::sum_sapi
    sapi::test_main
    sapi::testing
  )
  gtest_discover_tests_xcompile(sapi_test)
endif()

# Install headers and libraries, excluding tools, tests and examples
foreach(_dir IN ITEMS . sandbox2 sandbox2/network_proxy sandbox2/util util)
  get_property(_sapi_targets DIRECTORY ${_dir} PROPERTY BUILDSYSTEM_TARGETS)
  list(FILTER _sapi_targets INCLUDE REGEX ^\(sapi|sandbox2\).*)
  list(FILTER _sapi_targets EXCLUDE REGEX _test)
  install(TARGETS ${_sapi_targets}
          DESTINATION ${CMAKE_INSTALL_LIBDIR})
  set_property(TARGET ${_sapi_targets} PROPERTY SOVERSION 1)
endforeach()

file(GLOB_RECURSE _sapi_headers true ${CMAKE_CURRENT_LIST_DIR}/*.h)
list(FILTER _sapi_headers EXCLUDE REGEX /\(tools|examples\)/)
foreach(_file ${_sapi_headers})
  get_filename_component(_dir ${_file} DIRECTORY)
  string(REPLACE ${CMAKE_CURRENT_LIST_DIR} "" _dir ${_dir})
  install(FILES ${_file}
          DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sandboxed_api/${_dir})
endforeach()

configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/sapi.pc.in"
  "${PROJECT_BINARY_DIR}/sapi.pc"
  @ONLY
)

install(FILES "${PROJECT_BINARY_DIR}/sapi.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
