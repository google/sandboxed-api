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

_DEFAULT_VISIBILITY = [
    "@com_google_sandboxed_api//sandboxed_api:__subpackages__",
]

package(default_visibility = _DEFAULT_VISIBILITY)

package_group(
    name = "minielf_users",
    packages = [
    ],
)

licenses(["notice"])

cc_library(
    name = "bpf_helper",
    srcs = ["bpf_helper.c"],
    hdrs = ["bpf_helper.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "bpf_helper_test",
    srcs = ["bpf_helper_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":bpf_helper",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "minielf",
    srcs = ["minielf.cc"],
    hdrs = ["minielf.h"],
    copts = sapi_platform_copts(),
    visibility = _DEFAULT_VISIBILITY + [":minielf_users"],
    deps = [
        "@com_google_absl//absl/base:endian",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:util",
        "@com_google_sandboxed_api//sandboxed_api/util:raw_logging",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
    ],
)

cc_test(
    name = "minielf_test",
    srcs = ["minielf_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        ":testdata/chrome_grte_header",
        ":testdata/hello_world",
    ],
    features = ["-dynamic_link_test_srcs"],  # see go/dynamic_link_test_srcs
    deps = [
        ":maps_parser",
        ":minielf",
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/util:file_helpers",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
    ],
)

cc_library(
    name = "syscall_trap",
    srcs = ["syscall_trap.cc"],
    hdrs = ["syscall_trap.h"],
    copts = sapi_platform_copts(),
    deps = [
        "@com_google_absl//absl/log:check",
        "@com_google_sandboxed_api//sandboxed_api:config",
    ],
)

cc_library(
    name = "maps_parser",
    srcs = ["maps_parser.cc"],
    hdrs = ["maps_parser.h"],
    copts = sapi_platform_copts(),
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "maps_parser_test",
    srcs = ["maps_parser_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":maps_parser",
        "@com_google_absl//absl/status:statusor",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
    ],
)

cc_library(
    name = "pid_waiter",
    srcs = ["pid_waiter.cc"],
    hdrs = ["pid_waiter.h"],
    copts = sapi_platform_copts(),
)

cc_test(
    name = "pid_waiter_test",
    srcs = ["pid_waiter_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":pid_waiter",
        "@com_google_googletest//:gtest_main",
    ],
)
