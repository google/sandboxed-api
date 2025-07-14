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

# The 'tool' example demonstrates:
# - a sandbox executor, sandboxee would be another program
# - sandboxee sandboxed before execve
# - very lax, separate sandbox policy written with BPFDSL
# - expose file descriptors to executor with ReceiveFd
# - set limits, wall time, filesystem checks, asynchronous run
# - test to ensure sandbox executor runs sandboxee without issue

load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("//third_party/bazel_rules/rules_cc/cc:cc_binary.bzl", "cc_binary")

package(default_visibility = [
    "//sandboxed_api/sandbox2:__subpackages__",
])

licenses(["notice"])

# Executor
cc_binary(
    name = "sandbox2tool",
    srcs = ["sandbox2tool.cc"],
    copts = sapi_platform_copts(),
    deps = [
        "//sandboxed_api/sandbox2",
        "//sandboxed_api/sandbox2:util",
        "//sandboxed_api/sandbox2/allowlists:all_syscalls",
        "//sandboxed_api/sandbox2/allowlists:unrestricted_networking",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "//sandboxed_api/util:fileops",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/flags:usage",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
    ],
)

# Test
sh_test(
    name = "sandbox2tool_test",
    srcs = ["sandbox2tool_test.sh"],
    data = [":sandbox2tool"],
    tags = ["no_qemu_user_mode"],
)
