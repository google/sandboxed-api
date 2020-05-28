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
option(SAPI_DOWNLOAD_GFLAGS "Download gflags at config time" ON)
option(SAPI_DOWNLOAD_GLOG "Download glog at config time" ON)
option(SAPI_DOWNLOAD_PROTOBUF "Download protobuf at config time" ON)
option(SAPI_DOWNLOAD_LIBUNWIND "Download libunwind at config time" ON)
option(SAPI_DOWNLOAD_LIBCAP "Download libcap at config time" ON)
option(SAPI_DOWNLOAD_LIBFFI "Download libffi at config time" ON)

# Options for building examples
option(SAPI_ENABLE_EXAMPLES "Build example code" ON)
option(SAPI_DOWNLOAD_ZLIB "Download zlib at config time (only if SAPI_ENABLE_EXAMPLES is set)" ON)

option(SAPI_ENABLE_TESTS "Build unit tests" ON)
option(SAPI_ENABLE_GENERATOR "Build Clang based code generator from source" OFF)

option(SAPI_FORCE_COLOR_OUTPUT "Force colored compiler diagnostics when using Ninja" ON)
