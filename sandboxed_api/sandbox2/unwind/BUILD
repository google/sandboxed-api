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

load("@com_google_sandboxed_api//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("@com_google_sandboxed_api//sandboxed_api/bazel:proto.bzl", "sapi_proto_library")

package(default_visibility = [
    "@com_google_sandboxed_api//sandboxed_api/sandbox2:__subpackages__",
])

licenses(["notice"])

cc_library(
    name = "ptrace_hook",
    srcs = ["ptrace_hook.cc"],
    hdrs = ["ptrace_hook.h"],
    copts = sapi_platform_copts(),
    deps = [
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:syscall_trap",
    ],
)

cc_library(
    name = "unwind",
    srcs = ["unwind.cc"],
    hdrs = ["unwind.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":ptrace_hook",
        ":unwind_cc_proto",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:comms",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:maps_parser",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:minielf",
        "@com_google_sandboxed_api//sandboxed_api/util:file_helpers",
        "@com_google_sandboxed_api//sandboxed_api/util:raw_logging",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
        "@org_gnu_libunwind//:libunwind_ptrace",
    ],
)

sapi_proto_library(
    name = "unwind_proto",
    srcs = ["unwind.proto"],
)
