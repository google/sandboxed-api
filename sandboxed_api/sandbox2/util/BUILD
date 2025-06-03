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

DEFAULT_VISIBILITY = [
    "//sandboxed_api:__subpackages__",
]

package(default_visibility = DEFAULT_VISIBILITY)

licenses(["notice"])

cc_library(
    name = "bpf_helper",
    srcs = ["bpf_helper.c"],
    hdrs = ["bpf_helper.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_test(
    name = "bpf_helper_test",
    srcs = ["bpf_helper_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":bpf_helper",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "minielf",
    srcs = ["minielf.cc"],
    hdrs = ["minielf.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":elf_parser",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
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
        "//sandboxed_api:testing",
        "//sandboxed_api/util:file_helpers",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "elf_parser",
    srcs = ["elf_parser.cc"],
    hdrs = ["elf_parser.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        "//sandboxed_api:config",
        "//sandboxed_api/util:raw_logging",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/numeric:bits",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "library_resolver",
    srcs = ["library_resolver.cc"],
    hdrs = ["library_resolver.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":elf_parser",
        "//sandboxed_api:config",
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:raw_logging",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "syscall_trap",
    srcs = ["syscall_trap.cc"],
    hdrs = ["syscall_trap.h"],
    copts = sapi_platform_copts(),
    deps = [
        "//sandboxed_api:config",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "deadline_manager",
    srcs = ["deadline_manager.cc"],
    hdrs = ["deadline_manager.h"],
    copts = sapi_platform_copts(),
    deps = [
        "//sandboxed_api/sandbox2:util",
        "//sandboxed_api/util:thread",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "deadline_manager_test",
    srcs = ["deadline_manager_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":deadline_manager",
        "//sandboxed_api/util:thread",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "maps_parser",
    srcs = ["maps_parser.cc"],
    hdrs = ["maps_parser.h"],
    copts = sapi_platform_copts(),
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "maps_parser_test",
    srcs = ["maps_parser_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":maps_parser",
        "//sandboxed_api:testing",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "pid_waiter",
    srcs = ["pid_waiter.cc"],
    hdrs = ["pid_waiter.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":deadline_manager",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "pid_waiter_test",
    srcs = ["pid_waiter_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":pid_waiter",
        "//sandboxed_api/util:thread",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "seccomp_unotify",
    srcs = ["seccomp_unotify.cc"],
    hdrs = ["seccomp_unotify.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":bpf_helper",
        "//sandboxed_api/sandbox2:syscall",
        "//sandboxed_api/sandbox2:util",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:strerror",
        "//sandboxed_api/util:thread",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "seccomp_unotify_test",
    srcs = ["seccomp_unotify_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":seccomp_unotify",
        "//sandboxed_api:testing",
        "//sandboxed_api/util:fileops",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@googletest//:gtest_main",
    ],
)
