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

load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")

package(default_visibility = [
    "//sandboxed_api/sandbox2:__subpackages__",
])

licenses(["notice"])

cc_library(
    name = "server",
    srcs = ["server.cc"],
    hdrs = ["server.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":filtering",
        "//sandboxed_api/sandbox2:comms",
        "//sandboxed_api/util:fileops",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "client",
    srcs = ["client.cc"],
    hdrs = ["client.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        "//sandboxed_api/sandbox2:comms",
        "//sandboxed_api/sandbox2/util:syscall_trap",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "filtering",
    srcs = ["filtering.cc"],
    hdrs = ["filtering.h"],
    copts = sapi_platform_copts(),
    deps = [
        "//sandboxed_api/sandbox2:comms",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "filtering_test",
    srcs = ["filtering_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":filtering",
        "//sandboxed_api/util:status_matchers",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "testing",
    srcs = ["testing.cc"],
    hdrs = ["testing.h"],
    copts = sapi_platform_copts(),
    deps = [
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:status",
        "//sandboxed_api/util:thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)
