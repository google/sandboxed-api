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

"""Loads dependencies needed to compile Sandboxed API for 3rd-party consumers."""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("//sandboxed_api/bazel:repositories.bzl", "autotools_repository")

def sapi_deps():
    """Loads common dependencies needed to compile Sandboxed API."""

    # Bazel Skylib, needed by newer Protobuf builds
    maybe(
        http_archive,
        name = "bazel_skylib",
        urls = [
            "https://github.com/bazelbuild/bazel-skylib/releases/download/1.2.1/bazel-skylib-1.2.1.tar.gz",
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.2.1/bazel-skylib-1.2.1.tar.gz",
        ],
        sha256 = "f7be3474d42aae265405a592bb7da8e171919d74c16f082a5457840f06054728",  # 2022-03-10
    )

    # Abseil
    maybe(
        http_archive,
        name = "com_google_absl",
        sha256 = "610fa79ad54a80533447ba2bb3c91b7395c8d46111fc1d6b9de12f2d88e66dbb",  # 2022-04-04
        strip_prefix = "abseil-cpp-6f43f5bb398b6685575b36874e36cf1695734df1",
        urls = ["https://github.com/abseil/abseil-cpp/archive/6f43f5bb398b6685575b36874e36cf1695734df1.zip"],
    )
    maybe(
        http_archive,
        name = "com_google_absl_py",
        sha256 = "3d0278d88bbd52993f381d1e20887fa30f0556f6263b3f7bfcad62c69f39b38e",  # 2021-03-09
        strip_prefix = "abseil-py-9954557f9df0b346a57ff82688438c55202d2188",
        urls = ["https://github.com/abseil/abseil-py/archive/9954557f9df0b346a57ff82688438c55202d2188.zip"],
    )

    # Abseil-py dependency for Python 2/3 compatiblity
    maybe(
        http_archive,
        name = "six_archive",
        build_file = "@com_google_sandboxed_api//sandboxed_api:bazel/external/six.BUILD",
        sha256 = "30639c035cdb23534cd4aa2dd52c3bf48f06e5f4a941509c8bafd8ce11080259",  # 2020-05-21
        strip_prefix = "six-1.15.0",
        urls = ["https://pypi.python.org/packages/source/s/six/six-1.15.0.tar.gz"],
    )

    # gflags
    # TODO(cblichmann): Use Abseil flags once logging is in Abseil
    maybe(
        http_archive,
        name = "com_github_gflags_gflags",
        sha256 = "97312c67e5e0ad7fe02446ee124629ca7890727469b00c9a4bf45da2f9b80d32",  # 2019-11-13
        strip_prefix = "gflags-addd749114fab4f24b7ea1e0f2f837584389e52c",
        urls = ["https://github.com/gflags/gflags/archive/addd749114fab4f24b7ea1e0f2f837584389e52c.zip"],
    )

    # Google logging
    # TODO(cblichmann): Remove dependency once logging is in Abseil
    maybe(
        http_archive,
        name = "com_google_glog",
        sha256 = "feca3c7e29a693cab7887409756d89d342d4a992d54d7c5599bebeae8f7b50be",  # 2020-02-16
        strip_prefix = "glog-3ba8976592274bc1f907c402ce22558011d6fc5e",
        urls = ["https://github.com/google/glog/archive/3ba8976592274bc1f907c402ce22558011d6fc5e.zip"],
    )

    # Protobuf
    maybe(
        http_archive,
        name = "com_google_protobuf",
        sha256 = "dd513a79c7d7e45cbaeaf7655289f78fd6b806e52dbbd7018ef4e3cf5cff697a",  # 2021-04-08
        strip_prefix = "protobuf-3.15.8",
        urls = ["https://github.com/protocolbuffers/protobuf/archive/v3.15.8.zip"],
    )

    # libcap
    http_archive(
        name = "org_kernel_libcap",
        build_file = "@com_google_sandboxed_api//sandboxed_api:bazel/external/libcap.BUILD",
        sha256 = "260b549c154b07c3cdc16b9ccc93c04633c39f4fb6a4a3b8d1fa5b8a9c3f5fe8",  # 2019-04-16
        strip_prefix = "libcap-2.27",
        urls = ["https://www.kernel.org/pub/linux/libs/security/linux-privs/libcap2/libcap-2.27.tar.gz"],
    )

    # libffi
    autotools_repository(
        name = "org_sourceware_libffi",
        build_file = "@com_google_sandboxed_api//sandboxed_api:bazel/external/libffi.BUILD",
        sha256 = "653ffdfc67fbb865f39c7e5df2a071c0beb17206ebfb0a9ecb18a18f63f6b263",  # 2019-11-02
        strip_prefix = "libffi-3.3-rc2",
        urls = ["https://github.com/libffi/libffi/releases/download/v3.3-rc2/libffi-3.3-rc2.tar.gz"],
    )

    # libunwind
    autotools_repository(
        name = "org_gnu_libunwind",
        build_file = "@com_google_sandboxed_api//sandboxed_api:bazel/external/libunwind.BUILD",
        configure_args = [
            "--disable-documentation",
            "--disable-minidebuginfo",
            "--disable-shared",
            "--enable-ptrace",
        ],
        sha256 = "4a6aec666991fb45d0889c44aede8ad6eb108071c3554fcdff671f9c94794976",  # 2021-12-01
        strip_prefix = "libunwind-1.6.2",
        urls = ["https://github.com/libunwind/libunwind/releases/download/v1.6.2/libunwind-1.6.2.tar.gz"],
    )
