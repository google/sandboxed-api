# Copyright 2019 Google LLC. All Rights Reserved.
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

workspace(name = "com_google_sandboxed_api")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("//sandboxed_api/bazel:repositories.bzl", "autotools_repository")

# Bazel Skylib, needed by newer Protobuf builds
http_archive(
    name = "bazel_skylib",
    sha256 = "2ef429f5d7ce7111263289644d233707dba35e39696377ebab8b0bc701f7818e",
    url = "https://github.com/bazelbuild/bazel-skylib/releases/download/0.8.0/bazel-skylib.0.8.0.tar.gz",  # 2019-03-20
)

# Abseil
http_archive(
    name = "com_google_absl",
    sha256 = "57cadb5f4e35e479de395490994db5cd91a23b3a4d6fba85ffbe86590d70f606",  # 2019-05-07
    strip_prefix = "abseil-cpp-aa468ad75539619b47979911297efbb629c52e44",
    urls = ["https://github.com/abseil/abseil-cpp/archive/aa468ad75539619b47979911297efbb629c52e44.zip"],
)

# Abseil-py
http_archive(
    name = "com_google_absl_py",
    strip_prefix = "abseil-py-master",
    urls = ["https://github.com/abseil/abseil-py/archive/master.zip"],
)

# Abseil-py deps
http_archive(
    name = "six_archive",
    build_file = "//sandboxed_api:bazel/external/six.BUILD",
    sha256 = "105f8d68616f8248e24bf0e9372ef04d3cc10104f1980f54d57b2ce73a5ad56a",
    strip_prefix = "six-1.10.0",
    urls = [
        "http://mirror.bazel.build/pypi.python.org/packages/source/s/six/six-1.10.0.tar.gz",
        "https://pypi.python.org/packages/source/s/six/six-1.10.0.tar.gz",
    ],
)

http_archive(
    # NOTE: The name here is used in _enum_module.py to find the sys.path entry.
    name = "enum34_archive",
    build_file = "//sandboxed_api:bazel/external/enum34.BUILD",
    sha256 = "8ad8c4783bf61ded74527bffb48ed9b54166685e4230386a9ed9b1279e2df5b1",
    urls = [
        "https://mirror.bazel.build/pypi.python.org/packages/bf/3e/31d502c25302814a7c2f1d3959d2a3b3f78e509002ba91aea64993936876/enum34-1.1.6.tar.gz",
        "https://pypi.python.org/packages/bf/3e/31d502c25302814a7c2f1d3959d2a3b3f78e509002ba91aea64993936876/enum34-1.1.6.tar.gz",
    ],
)

# zlib
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
    sha256 = "c3e5e9fdd5004dcb542feda5ee4f0ff0744628baf8ed2dd5d66f8ca1197cb1a1",
    strip_prefix = "zlib-1.2.11",
    urls = ["https://www.zlib.net/zlib-1.2.11.tar.gz"],
)

# gflags
http_archive(
    name = "com_github_gflags_gflags",
    sha256 = "53b16091efa386ab11e33f018eef0ed489e0ab63554455293cbb0cc2a5f50e98",
    strip_prefix = "gflags-28f50e0fed19872e0fd50dd23ce2ee8cd759338e",
    urls = ["https://github.com/gflags/gflags/archive/28f50e0fed19872e0fd50dd23ce2ee8cd759338e.zip"],  # 2019-01-25
)

# GoogleTest/GoogleMock
http_archive(
    name = "com_google_googletest",
    sha256 = "70404b4a887fd8efce2179e9918e58cdac03245e575408ed87799696e816ecb8",
    strip_prefix = "googletest-f80d6644d4b451f568a2e7aea1e01e842eb242dc",
    urls = ["https://github.com/google/googletest/archive/f80d6644d4b451f568a2e7aea1e01e842eb242dc.zip"],  # 2019-02-05
)

# Google Benchmark
http_archive(
    name = "com_google_benchmark",
    strip_prefix = "benchmark-master",
    urls = ["https://github.com/google/benchmark/archive/master.zip"],
)

# Google logging
http_archive(
    name = "com_google_glog",
    sha256 = "74010e549e3555a11d3eb22b80f0040fa4f013a4b254b2d5ede12afcc92e690b",
    strip_prefix = "glog-41f4bf9cbc3e8995d628b459f6a239df43c2b84a",
    urls = ["https://github.com/google/glog/archive/41f4bf9cbc3e8995d628b459f6a239df43c2b84a.zip"],  # 2019-02-02
)

# Protobuf
http_archive(
    name = "com_google_protobuf",
    sha256 = "1e622ce4b84b88b6d2cdf1db38d1a634fe2392d74f0b7b74ff98f3a51838ee53",
    strip_prefix = "protobuf-3.8.0",
    urls = ["https://github.com/protocolbuffers/protobuf/archive/v3.8.0.zip"],
)

load("@com_google_protobuf//:protobuf_deps.bzl", "protobuf_deps")

protobuf_deps()

# libcap
http_archive(
    name = "org_kernel_libcap",
    build_file = "//sandboxed_api:bazel/external/libcap.BUILD",
    sha256 = "ef83108f77314e50bae926ae473f9b130b15240d17cbae05089e19c36a8676d6",
    strip_prefix = "libcap-2.13",
    urls = ["https://www.kernel.org/pub/linux/libs/security/linux-privs/libcap2/libcap-2.13.tar.gz"],
)

# libffi
autotools_repository(
    name = "org_sourceware_libffi",
    build_file = "//sandboxed_api:bazel/external/libffi.BUILD",
    sha256 = "403d67aabf1c05157855ea2b1d9950263fb6316536c8c333f5b9ab1eb2f20ecf",
    strip_prefix = "libffi-3.3-rc0",
    urls = ["https://github.com/libffi/libffi/releases/download/v3.3-rc0/libffi-3.3-rc0.tar.gz"],
)

# libunwind
autotools_repository(
    name = "org_gnu_libunwind",
    build_file = "//sandboxed_api:bazel/external/libunwind.BUILD",
    configure_args = [
        "--disable-documentation",
        "--disable-minidebuginfo",
        "--disable-shared",
        "--enable-ptrace",
    ],
    sha256 = "3f3ecb90e28cbe53fba7a4a27ccce7aad188d3210bb1964a923a731a27a75acb",
    strip_prefix = "libunwind-1.2.1",
    urls = ["https://github.com/libunwind/libunwind/releases/download/v1.2.1/libunwind-1.2.1.tar.gz"],  # v1.2.1
)
