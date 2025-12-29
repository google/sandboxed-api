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
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("//sandboxed_api/bazel:proto.bzl", "sapi_proto_library")

package(default_visibility = [
    "//sandboxed_api:__subpackages__",
])

licenses(["notice"])

# Another helper library with filesystem and path utility functions.
cc_library(
    name = "file_base",
    srcs = ["path.cc"],
    hdrs = ["path.h"],
    copts = sapi_platform_copts(),
    deps = ["@abseil-cpp//absl/strings"],
)

cc_test(
    name = "file_base_test",
    size = "small",
    srcs = ["path_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":file_base",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

# String file routines
cc_library(
    name = "file_helpers",
    srcs = ["file_helpers.cc"],
    hdrs = ["file_helpers.h"],
    copts = sapi_platform_copts(),
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "file_helpers_test",
    size = "small",
    srcs = ["file_helpers_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":file_helpers",
        "//sandboxed_api:testing",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
    ],
)

# Portable filesystem and path utility functions
cc_library(
    name = "fileops",
    srcs = ["fileops.cc"],
    hdrs = ["fileops.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":strerror",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "fileops_test",
    size = "small",
    srcs = ["fileops_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":file_helpers",
        ":fileops",
        "//sandboxed_api:testing",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

sapi_proto_library(
    name = "proto_arg",
    srcs = ["proto_arg.proto"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "proto_helper",
    srcs = ["proto_helper.cc"],
    hdrs = ["proto_helper.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":proto_arg_cc_proto",
        ":status",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

# Small support library emulating verbose logging using Abseil's raw logging
# facility.
cc_library(
    name = "raw_logging",
    srcs = ["raw_logging.cc"],
    hdrs = ["raw_logging.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":strerror",
        "@abseil-cpp//absl/base:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "runfiles",
    srcs = ["runfiles_bazel.cc"],
    hdrs = ["runfiles.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":file_base",
        ":raw_logging",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@bazel_tools//tools/cpp/runfiles",
    ],
)

sapi_proto_library(
    name = "status_proto",
    srcs = ["status.proto"],
)

# Implementations of utility functions not released with absl::Status.
cc_library(
    name = "status",
    srcs = ["status.cc"],
    hdrs = [
        "status.h",
        "status_macros.h",
    ],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":status_cc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
    ],
)

# Tests for the Status utility.
cc_test(
    name = "status_test",
    srcs = ["status_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":status",
        ":status_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:string_view",
        "@googletest//:gtest_main",
    ],
)

# Tests for the Status macros.
cc_test(
    name = "status_macros_test",
    srcs = ["status_macros_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":status",
        "//sandboxed_api:testing",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

# Internal thread-safe helper to format system error messages. Mostly
# equivalent to base/strerror.h.
cc_library(
    name = "strerror",
    srcs = ["strerror.cc"],
    hdrs = ["strerror.h"],
    copts = sapi_platform_copts(),
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_test(
    name = "strerror_test",
    srcs = ["strerror_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":strerror",
        ":thread",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "temp_file",
    srcs = ["temp_file.cc"],
    hdrs = ["temp_file.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":status",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "temp_file_test",
    srcs = ["temp_file_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":file_base",
        ":fileops",
        ":temp_file",
        "//sandboxed_api:testing",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "thread",
    hdrs = ["thread.h"],
    copts = sapi_platform_copts(),
    deps = [
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/strings:string_view",
    ],
)
