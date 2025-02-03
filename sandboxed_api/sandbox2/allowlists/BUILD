# Copyright 2025 Google LLC
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

# Description: Collection of allowlist tokens that are used to visibility
# restrict features in Sandbox2.

load("@com_google_sandboxed_api//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")

licenses(["notice"])

cc_library(
    name = "namespaces",
    hdrs = ["namespaces.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "testonly_namespaces",
    testonly = True,
    hdrs = ["namespaces.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)
