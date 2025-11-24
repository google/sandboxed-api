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

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("//sandboxed_api/bazel:proto.bzl", "sapi_proto_library")

package(default_visibility = [
    "//sandboxed_api/sandbox2:__subpackages__",
])

licenses(["notice"])

cc_library(
    name = "accessors",
    srcs = [
        "accessors.cc",
        "accessors_internal.c",
        "accessors_internal.h",
    ],
    hdrs = ["accessors.h"],
    copts = sapi_platform_copts() + [
        "-Ithird_party/libunwind/include",
        "-Ithird_party/libunwind/include/tdep",
        "-Ithird_party/libunwind/src",
        "-DHAVE_ELF_H",
        "-DHAVE_LINK_H",
    ],
    deps = [
        "//sandboxed_api/sandbox2:regs",
        "//sandboxed_api/sandbox2/util:maps_parser",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:raw_logging",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@libunwind//:libunwind_ptrace",
    ],
)

cc_library(
    name = "unwind",
    srcs = ["unwind.cc"],
    hdrs = ["unwind.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":accessors",
        ":unwind_cc_proto",
        "//sandboxed_api:config",
        "//sandboxed_api/sandbox2:comms",
        "//sandboxed_api/sandbox2:regs",
        "//sandboxed_api/sandbox2/util:demangle",
        "//sandboxed_api/sandbox2/util:maps_parser",
        "//sandboxed_api/sandbox2/util:minielf",
        "//sandboxed_api/util:file_helpers",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:raw_logging",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@libunwind//:libunwind_ptrace",
    ],
)

sapi_proto_library(
    name = "unwind_proto",
    srcs = ["unwind.proto"],
)
