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

# The 'static' example demonstrates:
# - separate executor and sandboxee
# - sandboxee already sandboxed, not using google3 and compiled statically
# - minimal syscall policy written with BPF macros
# - communication with file descriptors and MapFd
# - test to ensure sandbox executor runs sandboxee without issue

load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")

package(default_visibility = [
    "//sandboxed_api/sandbox2:__subpackages__",
])

licenses(["notice"])

# Executor
cc_binary(
    name = "static_sandbox",
    srcs = ["static_sandbox.cc"],
    copts = sapi_platform_copts(),
    data = [":static_bin"],
    tags = ["no_qemu_user_mode"],
    deps = [
        "//sandboxed_api:config",
        "//sandboxed_api/sandbox2",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "//sandboxed_api/util:runfiles",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
    ],
)

# Sandboxee
# security: disable=cc-static-no-pie
cc_binary(
    name = "static_bin",
    srcs = ["static_bin.cc"],
    copts = sapi_platform_copts(),
    linkopts = [
        "-static-pie",
        "-fuse-ld=bfd",
    ],
    linkstatic = 1,
)

sh_test(
    name = "static_sandbox_test",
    srcs = ["static_sandbox_test.sh"],
    data = [":static_sandbox"],
    tags = ["no_qemu_user_mode"],
)
