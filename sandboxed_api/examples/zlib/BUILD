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

# Description: Sandboxed API reimplementation of zlib's zpipe.c example.

load("@com_google_sandboxed_api//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("@com_google_sandboxed_api//sandboxed_api/bazel:sapi.bzl", "sapi_library")

package(default_visibility = ["@com_google_sandboxed_api//sandboxed_api:__subpackages__"])

licenses(["notice"])

sapi_library(
    name = "zlib-sapi",
    srcs = [],
    hdrs = [],
    functions = [
        "deflateInit_",
        "deflate",
        "deflateEnd",
    ],
    generator_version = 1,
    lib = "@net_zlib//:zlib",
    lib_name = "Zlib",
    namespace = "sapi::zlib",
)

cc_binary(
    name = "main_zlib",
    srcs = ["main_zlib.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":zlib-sapi",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:globals",
        "@com_google_absl//absl/log:initialize",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_sandboxed_api//sandboxed_api:vars",
    ],
)

# Tests input/output conversion of zlib and compares with golden files
sh_test(
    name = "main_zlib_test",
    srcs = ["main_zlib_test.sh"],
    data = [
        ":main_zlib",
    ],
    tags = ["notsan"],
)
