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

# The 'custom_fork' example demonstrates how to:
# - create a custom fork-server, which will prepare and fork a sandboxee
#   from the current process

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")

licenses(["notice"])

# Executor
cc_binary(
    name = "custom_fork_sandbox",
    srcs = ["custom_fork_sandbox.cc"],
    copts = sapi_platform_copts(),
    data = [":custom_fork_bin"],
    deps = [
        "//sandboxed_api:config",
        "//sandboxed_api/sandbox2",
        "//sandboxed_api/sandbox2:comms",
        "//sandboxed_api/sandbox2:fork_client",
        "//sandboxed_api/util:runfiles",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/time",
    ],
)

# Sandboxee
cc_binary(
    name = "custom_fork_bin",
    srcs = ["custom_fork_bin.cc"],
    copts = sapi_platform_copts(),
    deps = [
        "//sandboxed_api/sandbox2:comms",
        "//sandboxed_api/sandbox2:forkingclient",
        "//sandboxed_api/util:raw_logging",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
    ],
)

# Test
sh_test(
    name = "custom_fork_sandbox_test",
    srcs = ["custom_fork_sandbox_test.sh"],
    data = [":custom_fork_sandbox"],
    tags = ["no_qemu_user_mode"],
)
