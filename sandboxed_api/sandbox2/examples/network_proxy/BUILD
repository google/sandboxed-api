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

# The 'network proxy' example demonstrates how to use network proxy server.

load("@com_google_sandboxed_api//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")

package(default_visibility = [
    "@com_google_sandboxed_api//sandboxed_api/sandbox2:__subpackages__",
])

licenses(["notice"])

# Executor
cc_binary(
    name = "networkproxy_sandbox",
    srcs = ["networkproxy_sandbox.cc"],
    copts = sapi_platform_copts(),
    data = [":networkproxy_bin"],
    deps = [
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:globals",
        "@com_google_absl//absl/log:initialize",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:comms",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/network_proxy:testing",
        "@com_google_sandboxed_api//sandboxed_api/util:runfiles",
    ],
)

# Sandboxee
cc_binary(
    name = "networkproxy_bin",
    srcs = ["networkproxy_bin.cc"],
    copts = sapi_platform_copts(),
    deps = [
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:globals",
        "@com_google_absl//absl/log:initialize",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:client",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:comms",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/network_proxy:client",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
    ],
)

# Test
sh_test(
    name = "networkproxy_sandbox_test",
    srcs = ["networkproxy_sandbox_test.sh"],
    data = [":networkproxy_sandbox"],
    tags = ["no_qemu_user_mode"],
)
