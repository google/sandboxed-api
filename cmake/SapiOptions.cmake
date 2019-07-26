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

# These options determine whether CMake should download the libraries that
# Sandboxed API depends on at configure time.
# The CMake script SapiDeps.cmake checks for the presence of certain build
# targets to determine whether a library can be used. Disabling the options
# below enables embedding projects to selectively override/replace these
# dependencies. This is useful for cases where embedding projects already
# depend on some of these libraries (e.g. Abseil).
option(SAPI_USE_ABSL "Download Abseil at config time" ON)
option(SAPI_USE_GOOGLETEST "Download googletest at config time" ON)
option(SAPI_USE_GFLAGS "Download gflags at config time" ON)
option(SAPI_USE_GLOG "Download glog at config time" ON)
option(SAPI_USE_PROTOBUF "Download protobuf at config time" ON)
option(SAPI_USE_LIBUNWIND "Download libunwind at config time" ON)
# TODO(cblichmann): These two are not currently implemented
option(SAPI_USE_LIBCAP "Download libcap at config time" ON)
option(SAPI_USE_LIBFFI "Download libffi at config time" ON)

option(SAPI_ENABLE_EXAMPLES "Build example code" ON)
option(SAPI_ENABLE_TESTS "Build unit tests" ON)
