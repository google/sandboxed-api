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

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")

package(default_visibility = ["//sandboxed_api:__subpackages__"])

licenses(["notice"])

exports_files(["LICENSE"])

cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    copts = sapi_platform_copts(),
    deps = [
        "@abseil-cpp//absl/base:config",
    ],
)

cc_library(
    name = "embed_file",
    srcs = [
        "embed_file.cc",
        "file_toc.h",
    ],
    hdrs = ["embed_file.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        "//sandboxed_api/sandbox2:util",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:raw_logging",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_test(
    name = "embed_file_test",
    srcs = ["embed_file_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":embed_file",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

# The main Sandboxed-API library
cc_library(
    name = "sapi",
    srcs = [
        "sandbox.cc",
        "sandbox2_backend.cc",
        "sandbox2_rpcchannel.cc",
        "sandbox2_rpcchannel.h",
        "transaction.cc",
    ],
    hdrs = [
        # TODO(hamacher): Remove reexport workaround as soon as the buildsystem
        #                 supports this usecase.
        "embed_file.h",
        "sandbox.h",
        "sandbox2_backend.h",
        "sandbox_config.h",
        "transaction.h",
    ],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":call",
        ":config",
        ":embed_file",
        ":rpcchannel",
        ":var_type",
        ":vars",
        "//sandboxed_api/sandbox2",
        "//sandboxed_api/sandbox2:comms",
        "//sandboxed_api/sandbox2:fork_client",
        "//sandboxed_api/sandbox2:util",
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:runfiles",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:dynamic_annotations",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

# Definitions shared between sandboxee and master used for higher-level IPC.
cc_library(
    name = "call",
    hdrs = ["call.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [":var_type"],
)

cc_library(
    name = "lenval_core",
    hdrs = ["lenval_core.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "var_type",
    hdrs = ["var_type.h"],
    copts = sapi_platform_copts(),
)

# Variable hierarchy
cc_library(
    name = "vars",
    srcs = [
        "var_abstract.cc",
        "var_int.cc",
        "var_lenval.cc",
    ],
    hdrs = [
        # TODO(sroettger): Remove reexport of the rpcchannel.h header.
        "rpcchannel.h",
        "var_abstract.h",
        "var_array.h",
        "var_int.h",
        "var_lenval.h",
        "var_proto.h",
        "var_ptr.h",
        "var_reg.h",
        "var_struct.h",
        "var_void.h",
        "vars.h",
    ],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":call",
        ":lenval_core",
        ":rpcchannel",
        ":var_type",
        "//sandboxed_api/util:proto_helper",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
        "@abseil-cpp//absl/utility",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

cc_library(
    name = "rpcchannel",
    hdrs = ["rpcchannel.h"],
    visibility = ["//visibility:private"],
    deps = [
        ":call",
        ":var_type",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "function_call_helper",
    hdrs = ["function_call_helper.h"],
    copts = sapi_platform_copts(),
    # Note that this is not a public API. It is used by the generated code and by the
    # `call_message_handler` below, but should not be used directly by API users.
    visibility = ["//visibility:public"],
    deps = [
        ":call",
        ":lenval_core",
        ":var_type",
        "//sandboxed_api/util:proto_arg_cc_proto",
        "//sandboxed_api/util:proto_helper",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@com_google_protobuf//:protobuf",
    ],
)

# A stub to be linked in with SAPI libraries
cc_library(
    name = "client",
    srcs = ["client.cc"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":call",
        ":var_type",
        "//sandboxed_api/sandbox2:comms",
        "//sandboxed_api/sandbox2:forkingclient",
        "//sandboxed_api/sandbox2:logsink",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:dynamic_annotations",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:flags",
        "@abseil-cpp//absl/log:initialize",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "call_message_handler",
    srcs = ["call_message_handler.cc"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":call",
        ":function_call_helper",
        ":var_type",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:initialize",
        "@libffi",
    ],
)

# Utility library for writing tests
cc_library(
    name = "testing",
    testonly = 1,
    srcs = ["testing.cc"],
    hdrs = ["testing.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":config",
        "//sandboxed_api/sandbox2:policybuilder",
        "//sandboxed_api/sandbox2/allowlists:testonly_all_syscalls",
        "//sandboxed_api/sandbox2/allowlists:testonly_map_exec",
        "//sandboxed_api/util:file_base",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest",
    ],
)

cc_library(
    name = "annotations",
    srcs = ["annotations_internal.h"],
    hdrs = ["annotations.h"],
    visibility = ["//visibility:public"],
)
