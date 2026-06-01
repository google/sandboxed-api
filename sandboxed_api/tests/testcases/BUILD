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

load("//sandboxed_api/bazel:sapi.bzl", "cc_sandboxed_library", "cc_sandboxed_library_test")

package(default_visibility = ["//visibility:private"])

licenses(["notice"])

cc_sandboxed_library(
    name = "replacement_library",
    srcs = ["replaced_library.cc"],
    hdrs = [
        "replaced_library.h",
        "replaced_library_enum.h",
        "replaced_library_struct.h",
    ],
    annotations = ["lwbox_sandbox.cc"],
    deps = [
        "//sandboxed_api:annotations",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_sandboxed_library(
    name = "replacement_library2",
    srcs = ["replaced_library.cc"],
    hdrs = [
        "replaced_library.h",
        "replaced_library_enum.h",
        "replaced_library_struct.h",
    ],
    annotations = ["lwbox_ignore.cc"],
    deps = [
        "//sandboxed_api:annotations",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_sandboxed_library_test(
    name = "replacement_test",
    srcs = ["replaced_library_test.cc"],
    lib = ":replacement_library",
    deps = ["@googletest//:gtest_main"],
)

cc_sandboxed_library_test(
    name = "replacement2_test",
    srcs = ["replaced_library_test.cc"],
    lib = ":replacement_library2",
    deps = ["@googletest//:gtest_main"],
)

cc_sandboxed_library(
    name = "replacement_library_complex_size",
    srcs = ["replaced_library_complex_size.cc"],
    hdrs = [
        "replaced_library_complex_size.h",
    ],
    annotations = ["lwbox_complex_size_sandbox.cc"],
    deps = [
        "//sandboxed_api:annotations",
    ],
)

cc_sandboxed_library_test(
    name = "replacement_complex_size_test",
    srcs = ["replaced_library_complex_size_test.cc"],
    lib = ":replacement_library_complex_size",
    deps = [
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)
