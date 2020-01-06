# Copyright 2020 Google LLC. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
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
        sha256 = "1dde365491125a3db70731e25658dfdd3bc5dbdfd11b840b3e987ecf043c7ca0",  # 2019-07-12
        url = "https://github.com/bazelbuild/bazel-skylib/releases/download/0.9.0/bazel_skylib-0.9.0.tar.gz",
    )

    # Abseil
    maybe(
        http_archive,
        name = "com_google_absl",
        sha256 = "d8bc776c9702c7875c64410d1380cf3f3c0f75d5df9be08218589579604c539e",  # 2019-11-19
        strip_prefix = "abseil-cpp-8ba96a8244bbe334d09542e92d566673a65c1f78",
        urls = ["https://github.com/abseil/abseil-cpp/archive/8ba96a8244bbe334d09542e92d566673a65c1f78.zip"],
    )
    maybe(
        http_archive,
        name = "com_google_absl_py",
        sha256 = "51e9bbd6fbfedbad5627a782b6912c48a9a46f4b4095389cee586c9d80f6a56e",  # 2019-10-25
        strip_prefix = "abseil-py-62b0407d5e6cd3912d2c7d130cffdf6613018260",
        urls = ["https://github.com/abseil/abseil-py/archive/62b0407d5e6cd3912d2c7d130cffdf6613018260.zip"],
    )

    # Abseil-py dependency for Python 2/3 compatiblity
    maybe(
        http_archive,
        name = "six_archive",
        build_file = "@com_google_sandboxed_api//sandboxed_api:bazel/external/six.BUILD",
        sha256 = "d16a0141ec1a18405cd4ce8b4613101da75da0e9a7aec5bdd4fa804d0e0eba73",  # 2018-12-10
        strip_prefix = "six-1.12.0",
        urls = [
            "https://mirror.bazel.build/pypi.python.org/packages/source/s/six/six-1.12.0.tar.gz",
            "https://pypi.python.org/packages/source/s/six/six-1.12.0.tar.gz",
        ],
    )

    # gflags
    # TODO(cblichmann): Use Abseil flags once compatible with logging
    maybe(
        http_archive,
        name = "com_github_gflags_gflags",
        sha256 = "2c3403730ae711b29161b27380548a57204f683cb7152aa645657dcf6c57f72a",  # 2019-11-13
        strip_prefix = "gflags-d9b184bd0026b16bb4c2fded75d56fb2cce50d66",
        urls = ["https://github.com/gflags/gflags/archive/d9b184bd0026b16bb4c2fded75d56fb2cce50d66.zip"],
    )

    # Google logging
    maybe(
        http_archive,
        name = "com_google_glog",
        sha256 = "dbe787f2a7cf1146f748a191c99ae85d6b931dd3ebdcc76aa7ccae3699149c67",  # 2019-11-04
        strip_prefix = "glog-925858d9969d8ee22aabc3635af00a37891f4e25",
        urls = ["https://github.com/google/glog/archive/925858d9969d8ee22aabc3635af00a37891f4e25.zip"],
    )

    # Protobuf
    maybe(
        http_archive,
        name = "com_google_protobuf",
        sha256 = "678d91d8a939a1ef9cb268e1f20c14cd55e40361dc397bb5881e4e1e532679b1",  # 2019-10-29
        strip_prefix = "protobuf-3.10.1",
        urls = ["https://github.com/protocolbuffers/protobuf/archive/v3.10.1.zip"],
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
        sha256 = "3f3ecb90e28cbe53fba7a4a27ccce7aad188d3210bb1964a923a731a27a75acb",  # 2017-06-15
        strip_prefix = "libunwind-1.2.1",
        urls = ["https://github.com/libunwind/libunwind/releases/download/v1.2.1/libunwind-1.2.1.tar.gz"],
    )
