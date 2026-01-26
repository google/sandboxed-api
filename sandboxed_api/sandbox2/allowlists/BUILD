# Copyright 2025 Google LLC
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

# Description: Collection of allowlist tokens that are used to visibility
# restrict features in Sandbox2.
#
# In order to restrict these functionalities, you will need to remove the
# default visibility in a target (currently //visibility:public) with the
# appropriate visibility list for your targets.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")

licenses(["notice"])

cc_library(
    name = "all_syscalls",
    hdrs = ["all_syscalls.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "testonly_all_syscalls",
    testonly = True,
    hdrs = ["all_syscalls.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "map_exec",
    hdrs = ["map_exec.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "testonly_map_exec",
    testonly = True,
    hdrs = ["map_exec.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "mount_propagation",
    hdrs = ["mount_propagation.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "testonly_mount_propagation",
    testonly = True,
    hdrs = ["mount_propagation.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "write_executable",
    hdrs = ["write_executable.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "testonly_write_executable",
    testonly = True,
    hdrs = ["write_executable.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "seccomp_speculation",
    hdrs = ["seccomp_speculation.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "testonly_seccomp_speculation",
    testonly = True,
    hdrs = ["seccomp_speculation.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "unrestricted_networking",
    hdrs = ["unrestricted_networking.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "testonly_unrestricted_networking",
    testonly = True,
    hdrs = ["unrestricted_networking.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "namespaces",
    hdrs = ["namespaces.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "testonly_namespaces",
    testonly = True,
    hdrs = ["namespaces.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "trace_all_syscalls",
    hdrs = ["trace_all_syscalls.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "testonly_trace_all_syscalls",
    testonly = True,
    hdrs = ["trace_all_syscalls.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)
