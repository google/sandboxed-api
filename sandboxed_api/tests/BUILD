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
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("//sandboxed_api/bazel:sapi.bzl", "sapi_library")

package(default_visibility = ["//visibility:private"])

licenses(["notice"])

cc_test(
    name = "sapi_test",
    srcs = ["sapi_test.cc"],
    copts = sapi_platform_copts(),
    tags = ["local"],
    deps = [
        ":sapi_test-sapi",
        "//sandboxed_api:sapi",
        "//sandboxed_api:testing",
        "//sandboxed_api:vars",
        "//sandboxed_api/examples/stringop:stringop-sapi",
        "//sandboxed_api/examples/stringop:stringop_params_cc_proto",
        "//sandboxed_api/examples/sum:sum-sapi",
        "//sandboxed_api/sandbox2:result",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:status",
        "//sandboxed_api/util:thread",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@google_benchmark//:benchmark",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "sapi_test_lib",
    srcs = [
        "sapi_test_lib_cpp.cc",
    ],
    deps = [
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/types:span",
    ],
)

sapi_library(
    name = "sapi_test-sapi",
    functions = [
        "accumulate",
    ],
    input_files = [
        "sapi_test_lib_cpp.cc",
    ],
    lib = ":sapi_test_lib",
    lib_name = "SapiTest",
    namespace = "",
)
