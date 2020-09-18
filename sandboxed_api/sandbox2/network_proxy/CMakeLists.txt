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

# sandboxed_api/sandbox2/network_proxy:server
add_library(sandbox2_network_proxy_server STATIC
  server.cc
  server.h
)
add_library(sandbox2::network_proxy_server ALIAS sandbox2_network_proxy_server)
target_link_libraries(sandbox2_network_proxy_server PRIVATE
  absl::memory
  glog::glog
  sandbox2::comms
  sandbox2::fileops
  sandbox2::network_proxy_filtering
  sapi::base
)

# sandboxed_api/sandbox2/network_proxy:filtering
add_library(sandbox2_network_proxy_filtering STATIC
  filtering.cc
  filtering.h
)
add_library(sandbox2::network_proxy_filtering ALIAS sandbox2_network_proxy_filtering)
target_link_libraries(sandbox2_network_proxy_filtering
  PRIVATE absl::memory
          absl::status
          glog::glog
          sandbox2::comms
          sandbox2::fileops
          sapi::base
  PUBLIC sapi::status
)

# sandboxed_api/sandbox2/network_proxy:client
add_library(sandbox2_network_proxy_client STATIC
  client.cc
  client.h
)
add_library(sandbox2::network_proxy_client ALIAS sandbox2_network_proxy_client)
target_link_libraries(sandbox2_network_proxy_client PRIVATE
  absl::strings
  absl::synchronization
  glog::glog
  sandbox2::comms
  sandbox2::config
  sandbox2::strerror
  sapi::base
  sapi::status
)

if(SAPI_ENABLE_TESTS)
  # sandboxed_api/sandbox2/network_proxy:filtering_test
  add_executable(filtering_test
    filtering_test.cc
  )
  target_link_libraries(filtering_test PRIVATE
    absl::strings
    glog::glog
    gflags::gflags
    sandbox2::network_proxy_filtering
    sandbox2::testing
    sapi::base
    sapi::status_matchers
    sapi::test_main
  )
  gtest_discover_tests(filtering_test)
endif()
