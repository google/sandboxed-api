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

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("//sandboxed_api/bazel:sapi.bzl", "cc_sandboxed_library", "cc_sandboxed_library_test")

package(default_visibility = ["//visibility:private"])

licenses(["notice"])

cc_library(
    name = "replaced_library",
    srcs = ["replaced_library.cc"],
    hdrs = ["replaced_library.h"],
    deps = [
        "//sandboxed_api:annotations",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_sandboxed_library(
    name = "replacement_library",
    srcs = ["lwbox_sandbox.cc"],
    lib = ":replaced_library",
)

cc_sandboxed_library(
    name = "replacement_library2",
    srcs = ["lwbox_ignore.cc"],
    lib = ":replaced_library",
)

cc_sandboxed_library_test(
    name = "replaced_test",
    srcs = ["replaced_library_test.cc"],
    lib = ":replaced_library",
    sandboxed_lib = ":replacement_library",
    deps = ["@googletest//:gtest_main"],
)

cc_sandboxed_library_test(
    name = "replaced2_test",
    srcs = ["replaced_library_test.cc"],
    lib = ":replaced_library",
    sandboxed_lib = ":replacement_library2",
    deps = ["@googletest//:gtest_main"],
)
