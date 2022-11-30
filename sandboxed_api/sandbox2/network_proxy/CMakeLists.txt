# Copyright 2019 Google LLC. All Rights Reserved.
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

# sandboxed_api/sandbox2/network_proxy:server
add_library(sandbox2_network_proxy_server ${SAPI_LIB_TYPE}
  server.cc
  server.h
)
add_library(sandbox2::network_proxy_server ALIAS sandbox2_network_proxy_server)
target_link_libraries(sandbox2_network_proxy_server PRIVATE
  sandbox2::comms
  sapi::fileops
  sandbox2::network_proxy_filtering
  sapi::base
)

# sandboxed_api/sandbox2/network_proxy:filtering
add_library(sandbox2_network_proxy_filtering ${SAPI_LIB_TYPE}
  filtering.cc
  filtering.h
)
add_library(sandbox2::network_proxy_filtering ALIAS sandbox2_network_proxy_filtering)
target_link_libraries(sandbox2_network_proxy_filtering
  PRIVATE absl::status
          sandbox2::comms
          sapi::fileops
          sapi::base
  PUBLIC absl::statusor
         absl::log
         sapi::strerror
         sapi::status
)

# sandboxed_api/sandbox2/network_proxy:client
add_library(sandbox2_network_proxy_client ${SAPI_LIB_TYPE}
  client.cc
  client.h
)
add_library(sandbox2::network_proxy_client ALIAS sandbox2_network_proxy_client)
target_link_libraries(sandbox2_network_proxy_client PRIVATE
  absl::strings
  absl::synchronization
  absl::log
  sandbox2::comms
  sandbox2::syscall_trap
  sapi::strerror
  sapi::base
  sapi::status
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING)
  # sandboxed_api/sandbox2/network_proxy:filtering_test
  add_executable(sandbox2_filtering_test
    filtering_test.cc
  )
  set_target_properties(sandbox2_filtering_test PROPERTIES
    OUTPUT_NAME filtering_test
  )
  target_link_libraries(sandbox2_filtering_test PRIVATE
    absl::check
    absl::strings
    sandbox2::network_proxy_filtering
    sapi::testing
    sapi::base
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sandbox2_filtering_test)
endif()
