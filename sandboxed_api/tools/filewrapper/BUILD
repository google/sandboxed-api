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

load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("//sandboxed_api/bazel:embed_data.bzl", "sapi_cc_embed_data")

licenses(["notice"])

# An implicit dependency of all "sapi_cc_embed_data" rules that builds
# embedded data into .cc files.
cc_binary(
    name = "filewrapper",
    srcs = ["filewrapper.cc"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:raw_logging",
        "//sandboxed_api/util:strerror",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

sapi_cc_embed_data(
    name = "filewrapper_embedded",
    srcs = ["testdata/filewrapper_embedded.bin"],
)

cc_test(
    name = "filewrapper_test",
    srcs = ["filewrapper_test.cc"],
    copts = sapi_platform_copts(),
    data = ["testdata/filewrapper_embedded.bin"],
    deps = [
        ":filewrapper_embedded",
        "//sandboxed_api:testing",
        "//sandboxed_api/util:file_helpers",
        "//sandboxed_api/util:status_matchers",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
    ],
)
