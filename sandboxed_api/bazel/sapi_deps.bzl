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
load("//sandboxed_api/bazel:llvm_config.bzl", "llvm_configure")
load("//sandboxed_api/bazel:repositories.bzl", "autotools_repository")

def sapi_deps():
    """Loads common dependencies needed to compile Sandboxed API."""

    # Bazel rules_python
    maybe(
        http_archive,
        name = "rules_python",
        sha256 = "c6fb25d0ba0246f6d5bd820dd0b2e66b339ccc510242fd4956b9a639b548d113",  # 2024-10-27
        strip_prefix = "rules_python-0.37.2",
        urls = ["https://github.com/bazelbuild/rules_python/releases/download/0.37.2/rules_python-0.37.2.tar.gz"],
    )

    # Bazel Skylib
    maybe(
        http_archive,
        name = "bazel_skylib",
        sha256 = "bc283cdfcd526a52c3201279cda4bc298652efa898b10b4db0837dc51652756f",  # 2024-06-03
        urls = [
            "https://mirror.bazel.build/github.com/bazelbuild/bazel-skylib/releases/download/1.7.1/bazel-skylib-1.7.1.tar.gz",
            "https://github.com/bazelbuild/bazel-skylib/releases/download/1.7.1/bazel-skylib-1.7.1.tar.gz",
        ],
    )

    # Abseil
    maybe(
        http_archive,
        name = "com_google_absl",
        sha256 = "571549a0fa17ebf46f517541bb8d66fe369493963d463409fe61f2b8a44eb2dc",  # 2024-04-05
        strip_prefix = "abseil-cpp-fa588813c4b2d931737bbe7c4b4f7fa6ed7509db",
        urls = ["https://github.com/abseil/abseil-cpp/archive/fa588813c4b2d931737bbe7c4b4f7fa6ed7509db.zip"],
    )
    maybe(
        http_archive,
        name = "com_google_absl_py",
        sha256 = "8a3d0830e4eb4f66c4fa907c06edf6ce1c719ced811a12e26d9d3162f8471758",  # 2024-01-16
        strip_prefix = "abseil-py-2.1.0",
        urls = ["https://github.com/abseil/abseil-py/archive/refs/tags/v2.1.0.tar.gz"],
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

    # Protobuf
    maybe(
        http_archive,
        name = "com_google_protobuf",
        sha256 = "b2340aa47faf7ef10a0328190319d3f3bee1b24f426d4ce8f4253b6f27ce16db",  # 2024-09-18
        strip_prefix = "protobuf-28.2",
        urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v28.2/protobuf-28.2.tar.gz"],
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

    # GoogleTest/GoogleMock
    maybe(
        http_archive,
        name = "com_google_googletest",
        sha256 = "a217118c2c36a3632b594af7ff98111a65bb2b980b726a7fa62305e02a998440",  # 2023-06-06
        strip_prefix = "googletest-334704df263b480a3e9e7441ed3292a5e30a37ec",
        urls = ["https://github.com/google/googletest/archive/334704df263b480a3e9e7441ed3292a5e30a37ec.zip"],
    )

    # Google Benchmark
    maybe(
        http_archive,
        name = "com_google_benchmark",
        sha256 = "342705876335bf894147e052d0dac141fe15962034b41bef5aa59c4b279ca89c",  # 2023-05-30
        strip_prefix = "benchmark-604f6fd3f4b34a84ec4eb4db81d842fa4db829cd",
        urls = ["https://github.com/google/benchmark/archive/604f6fd3f4b34a84ec4eb4db81d842fa4db829cd.zip"],
    )

    # LLVM/libclang
    maybe(
        llvm_configure,
        name = "llvm-project",
        commit = "2c494f094123562275ae688bd9e946ae2a0b4f8b",  # 2022-03-31
        sha256 = "59b9431ae22f0ea5f2ce880925c0242b32a9e4f1ae8147deb2bb0fc19b53fa0d",
        system_libraries = True,  # Prefer system libraries
    )
