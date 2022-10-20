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

# If Sandboxed API is built from a plain checkout, built everything.
# Otherwise, skip tests and examples.
if(SAPI_PROJECT_IS_TOP_LEVEL)
  set(_sapi_enable_tests_examples_default ON)
else()
  set(_sapi_enable_tests_examples_default OFF)
endif()

# These options determine whether CMake should download the libraries that
# Sandboxed API depends on at configure time.
# The CMake script SapiDeps.cmake checks for the presence of certain build
# targets to determine whether a library can be used. Disabling the options
# below enables embedding projects to selectively override/replace these
# dependencies. This is useful for cases where embedding projects already
# depend on some of these libraries (e.g. Abseil).
option(SAPI_DOWNLOAD_ABSL "Download Abseil at config time" ON)
option(SAPI_DOWNLOAD_GOOGLETEST "Download googletest at config time" ON)
option(SAPI_DOWNLOAD_BENCHMARK "Download benchmark at config time" ON)
option(SAPI_DOWNLOAD_PROTOBUF "Download protobuf at config time" ON)
option(SAPI_DOWNLOAD_LIBUNWIND "Download libunwind at config time" ON)
option(SAPI_DOWNLOAD_LIBCAP "Download libcap at config time" ON)
option(SAPI_DOWNLOAD_LIBFFI "Download libffi at config time" ON)

# Options for building examples
option(SAPI_BUILD_EXAMPLES
  "If ON, build example code" ${_sapi_enable_tests_examples_default}
)
option(SAPI_DOWNLOAD_ZLIB
  "Download zlib at config time (only if SAPI_BUILD_EXAMPLES is set)"
  ${_sapi_enable_tests_examples_default}
)

option(SAPI_BUILD_TESTING
  "If ON, this will build all of Sandboxed API's own tests"
  ${_sapi_enable_tests_examples_default}
)
# Disabled by default, as this will download a lot of extra content.
option(SAPI_CONTRIB_BUILD_TESTING "Build tests for sandboxes in 'contrib'" OFF)

option(SAPI_ENABLE_GENERATOR
  "Build Clang based code generator from source" OFF
)

# This flag should be only enabled for embedded and resource-constrained
# environments.
option(SAPI_ENABLE_SHARED_LIBS "Build SAPI shared libs" OFF)

option(SAPI_HARDENED_SOURCE "Build with hardening compiler options" OFF)

option(SAPI_FORCE_COLOR_OUTPUT
  "Force colored compiler diagnostics when using Ninja" ON
)
