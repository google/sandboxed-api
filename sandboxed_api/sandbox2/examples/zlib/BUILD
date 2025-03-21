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

package(default_visibility = [
    "//sandboxed_api/sandbox2:__subpackages__",
])

licenses(["notice"])

# Executor
cc_binary(
    name = "zpipe_sandbox",
    srcs = ["zpipe_sandbox.cc"],
    copts = sapi_platform_copts(),
    data = [":zpipe"],
    deps = [
        "//sandboxed_api/sandbox2",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "//sandboxed_api/util:runfiles",
        "@abseil-cpp//absl/flags:flag",
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
cc_binary(
    name = "zpipe",
    srcs = ["zpipe.c"],
    copts = sapi_platform_copts(),
    features = ["fully_static_link"],
    deps = [
        "@net_zlib//:zlib",
    ],
)
