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
load("@rules_cc//cc:cc_test.bzl", "cc_test")
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

cc_library(
    name = "runtime",
    srcs = ["lwbox_runtime.cc"],
    hdrs = ["lwbox_runtime.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        "//sandboxed_api:sapi",
        "//sandboxed_api:vars",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "runtime_test",
    srcs = ["lwbox_runtime_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":runtime",
        "//sandboxed_api:call",
        "//sandboxed_api:sapi",
        "//sandboxed_api:var_type",
        "//sandboxed_api:vars",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)
