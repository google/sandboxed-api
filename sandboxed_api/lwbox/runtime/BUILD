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

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

cc_library(
    name = "sandbox_config",
    srcs = ["sandbox_config.cc"],
    hdrs = ["sandbox_config.h"],
    copts = sapi_platform_copts(),
    deps = [
        "//sandboxed_api:sandbox_config",
        "//sandboxed_api/sandbox2:policy",
        "//sandboxed_api/sandbox2:policybuilder",
        "//sandboxed_api/sandbox2/allowlists:seccomp_speculation",
    ],
)
