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

load("@com_google_sandboxed_api//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")

package(default_visibility = [
    "@com_google_sandboxed_api//sandboxed_api/sandbox2:__subpackages__",
])

licenses(["notice"])

# Executor
cc_binary(
    name = "zpipe_sandbox",
    srcs = ["zpipe_sandbox.cc"],
    copts = sapi_platform_copts(),
    data = [":zpipe"],
    deps = [
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/log:globals",
        "@com_google_absl//absl/log:initialize",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:bpf_helper",
        "@com_google_sandboxed_api//sandboxed_api/util:runfiles",
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
