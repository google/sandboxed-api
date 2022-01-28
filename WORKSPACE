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

workspace(name = "com_google_sandboxed_api")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("//sandboxed_api/bazel:sapi_deps.bzl", "sapi_deps")

# Load common dependencies, then Protobuf's
sapi_deps()

load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")

protobuf_deps()

# zlib, only needed for examples
http_archive(
    name = "net_zlib",
    build_file = "//sandboxed_api:bazel/external/zlib.BUILD",
    patch_args = ["-p1"],
    # This is a patch that removes the "OF" macro that is used in zlib function
    # definitions. It is necessary, because libclang, the library used by the
    # interface generator to parse C/C++ files contains a bug that manifests
    # itself with macros like this.
    # We are investigating better ways to avoid this issue. For most "normal"
    # C and C++ headers, parsing just works.
    patches = ["//sandboxed_api:bazel/external/zlib.patch"],
    sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",  # 2020-04-23
    strip_prefix = "zlib-1.2.11",
    urls = [
        "https://mirror.bazel.build/zlib.net/zlib-1.2.11.tar.gz",
        "https://www.zlib.net/zlib-1.2.11.tar.gz",
    ],
)

# GoogleTest/GoogleMock
maybe(
    http_archive,
    name = "com_google_googletest",
    sha256 = "1009ce4e75a64a4e61bcb2efaa256f9d54e6a859a2985cb6fa57c06d45356866",  # 2021-12-20
    strip_prefix = "googletest-9a32aee22d771387c494be2d8519fbdf46a713b2",
    urls = ["https://github.com/google/googletest/archive/9a32aee22d771387c494be2d8519fbdf46a713b2.zip"],
)

# Google Benchmark
maybe(
    http_archive,
    name = "com_google_benchmark",
    sha256 = "12663580821c69f5a71217433b58e96f061570f0e18d94891b82115fcdb4284d",  # 2021-12-14
    strip_prefix = "benchmark-3b3de69400164013199ea448f051d94d7fc7d81f",
    urls = ["https://github.com/google/benchmark/archive/3b3de69400164013199ea448f051d94d7fc7d81f.zip"],
)
