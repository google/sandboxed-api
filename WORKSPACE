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

load("//sandboxed_api/bazel:sapi_deps.bzl", "sapi_deps")

# Load Sandboxed API dependencies
sapi_deps()

load("@bazel_skylib//lib:versions.bzl", "versions")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load(
    "//sandboxed_api/bazel:llvm_config.bzl",
    "llvm_disable_optional_support_deps",
)
load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")

versions.check(minimum_bazel_version = "5.1.0")

protobuf_deps()

llvm_disable_optional_support_deps()

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
