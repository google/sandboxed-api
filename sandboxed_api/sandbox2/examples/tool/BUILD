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

load("@com_google_sandboxed_api//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")

package(default_visibility = [
    "@com_google_sandboxed_api//sandboxed_api/sandbox2:__subpackages__",
])

licenses(["notice"])

# Executor
cc_binary(
    name = "sandbox2tool",
    srcs = ["sandbox2tool.cc"],
    copts = sapi_platform_copts(),
    deps = [
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/flags:usage",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/log:globals",
        "@com_google_absl//absl/log:initialize",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:util",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:all_syscalls",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:unrestricted_networking",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:bpf_helper",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
    ],
)

# Test
sh_test(
    name = "sandbox2tool_test",
    srcs = ["sandbox2tool_test.sh"],
    data = [":sandbox2tool"],
    tags = ["no_qemu_user_mode"],
)
