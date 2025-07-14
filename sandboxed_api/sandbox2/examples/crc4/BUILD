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

# The 'crc4' example demonstrates:
# - Separate executor and sandboxee
# - Sandboxee enables sandboxing by calling SandboxMeHere()
# - Strict syscall policy
# - Using sandbox2::Comms for data exchange (IPC)
# - Test to ensure sandbox executor runs sandboxee without issue

load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("//third_party/bazel_rules/rules_cc/cc:cc_binary.bzl", "cc_binary")
load("//third_party/bazel_rules/rules_cc/cc:cc_test.bzl", "cc_test")

package(default_visibility = [
    "//sandboxed_api/sandbox2:__subpackages__",
])

licenses(["notice"])

# Executor
cc_binary(
    name = "crc4sandbox",
    srcs = ["crc4sandbox.cc"],
    copts = sapi_platform_copts(),
    data = [":crc4bin"],
    deps = [
        "//sandboxed_api/sandbox2",
        "//sandboxed_api/sandbox2:comms",
        "//sandboxed_api/sandbox2/allowlists:namespaces",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "//sandboxed_api/util:runfiles",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
    ],
)

# Sandboxee
cc_binary(
    name = "crc4bin",
    srcs = ["crc4bin.cc"],
    copts = sapi_platform_copts(),
    deps = [
        "//sandboxed_api/sandbox2:client",
        "//sandboxed_api/sandbox2:comms",
        "//sandboxed_api/sandbox2:util",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_test(
    name = "crc4sandbox_test",
    srcs = ["crc4sandbox_test.cc"],
    copts = sapi_platform_copts(),
    data = [":crc4sandbox"],
    tags = [
        "local",
        "no_qemu_user_mode",
    ],
    deps = [
        "//sandboxed_api:testing",
        "//sandboxed_api/sandbox2:util",
        "@abseil-cpp//absl/log",
        "@googletest//:gtest_main",
    ],
)
