# Copyright 2026 Google LLC
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

add_library(sapi_lwbox_sandbox_config ${SAPI_LIB_TYPE}
  sandbox_config.cc
  sandbox_config.h
)
add_library(sapi::lwbox_sandbox_config ALIAS sapi_lwbox_sandbox_config)
target_link_libraries(sapi_lwbox_sandbox_config
  PUBLIC
    absl::check
    absl::flags
    absl::status
    absl::statusor
    absl::time
    sandbox2::allowlists_seccomp_speculation
    sandbox2::limits
    sandbox2::policy
    sandbox2::policybuilder
    sandbox2::sandbox_config
    sapi::base
    sapi::checked_math
    sapi::embed_toc
    sapi::sandbox_config
    sapi::sapi
)

# Object library, like `alwayslink` in Bazel, so binaries link its definitions
# even when referenced through forward declarations.
add_library(sapi_lwbox_sandbox2_merged_sandbox_host OBJECT
  sandbox2_merged_sandbox_host.cc
)
add_library(sapi::lwbox_sandbox2_merged_sandbox_host ALIAS
  sapi_lwbox_sandbox2_merged_sandbox_host
)
target_link_libraries(sapi_lwbox_sandbox2_merged_sandbox_host
  PUBLIC
    absl::no_destructor
    sapi::base
    sapi::lwbox_sandbox_config
    sapi::sandbox2_backend
    sapi::sandbox_config
    sapi::sapi
)

add_library(sapi_lwbox_sandboxee_registry STATIC
  sandboxee_registry.cc
  sandboxee_registry.h
)
add_library(sapi::lwbox_sandboxee_registry ALIAS sapi_lwbox_sandboxee_registry)
target_link_libraries(sapi_lwbox_sandboxee_registry
  PUBLIC
    absl::check
    absl::core_headers
    absl::flat_hash_map
    absl::log
    absl::no_destructor
    absl::strings
    sapi::base
    sapi::call
    sapi::var_type
)

# Object library, so that binaries always link its static initializer, like
# `alwayslink` does in Bazel.
add_library(sapi_lwbox_sandbox2_merged_sandboxee OBJECT
  sandbox2_merged_sandboxee.cc
)
add_library(sapi::lwbox_sandbox2_merged_sandboxee ALIAS
  sapi_lwbox_sandbox2_merged_sandboxee
)
target_link_libraries(sapi_lwbox_sandbox2_merged_sandboxee
  PUBLIC
    sapi::base
    sapi::lwbox_sandboxee_registry
    sapi::client
)

add_library(sapi_lwbox_runtime ${SAPI_LIB_TYPE}
  lwbox_runtime.cc
  lwbox_runtime.h
)
add_library(sapi::lwbox_runtime ALIAS sapi_lwbox_runtime)
target_link_libraries(sapi_lwbox_runtime
  PUBLIC
    absl::any_invocable
    absl::check
    absl::core_headers
    absl::flat_hash_map
    absl::flat_hash_set
    absl::log
    absl::no_destructor
    absl::node_hash_map
    absl::span
    absl::status
    absl::statusor
    absl::strings
    absl::synchronization
    sapi::base
    sapi::checked_math
    sapi::sapi
    sapi::vars
)

if(BUILD_TESTING AND SAPI_BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
  add_executable(sapi_lwbox_runtime_test
    lwbox_runtime_test.cc
  )
  target_link_libraries(sapi_lwbox_runtime_test PRIVATE
    sapi::lwbox_runtime
    sapi::sapi
    sapi::vars
    absl::any_invocable
    absl::status
    absl::status_matchers
    absl::statusor
    absl::time
    absl::span
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sapi_lwbox_runtime_test)

  add_executable(sapi_lwbox_sandbox_config_test
    sandbox_config_test.cc
  )
  target_link_libraries(sapi_lwbox_sandbox_config_test PRIVATE
    sapi::lwbox_sandbox_config
    sapi::embed_toc
    sapi::sandbox_config
    sapi::sapi
    sandbox2::policy
    sandbox2::policybuilder
    absl::flags
    absl::flags_commandlineflag
    absl::flags_reflection
    absl::status
    absl::status_matchers
    absl::statusor
    absl::time
    sapi::test_main
  )
  gtest_discover_tests_xcompile(sapi_lwbox_sandbox_config_test)

  add_executable(sapi_lwbox_sandboxee_registry_test
    sandboxee_registry_test.cc
  )
  target_link_libraries(sapi_lwbox_sandboxee_registry_test PRIVATE
    sapi::test_main
    sapi::lwbox_sandbox2_merged_sandboxee
    sapi::lwbox_sandboxee_registry
    sapi::call
    sapi::var_type
  )
  gtest_discover_tests_xcompile(sapi_lwbox_sandboxee_registry_test)
endif()
