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

# Bazel rules_python
maybe(
    http_archive,
    name = "rules_python",
    sha256 = "5f5855c2a8af8fa9e09ed26720ed921f1a119f27cb041c5c137c8a5d3c8d9c55",  # 2024-04-05
    strip_prefix = "rules_python-4a615bec59b51d9d5f0675ec312c5b84e2eb792c",
    urls = ["https://github.com/bazelbuild/rules_python/archive/4a615bec59b51d9d5f0675ec312c5b84e2eb792c.zip"],
)

load("@rules_python//python:repositories.bzl", "py_repositories")

py_repositories()

load("//sandboxed_api/bazel:sapi_deps.bzl", "sapi_deps")

# Load Sandboxed API dependencies
sapi_deps()

load("@bazel_skylib//lib:versions.bzl", "versions")
versions.check(minimum_bazel_version = "5.1.0")

load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")

protobuf_deps()

load(
    "//sandboxed_api/bazel:llvm_config.bzl",
    "llvm_disable_optional_support_deps",
)

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
