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

package(default_visibility = ["@com_google_sandboxed_api//sandboxed_api:__subpackages__"])

licenses(["notice"])

exports_files(["LICENSE"])

cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    copts = sapi_platform_copts(),
    deps = [
        "@com_google_absl//absl/base:config",
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
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:util",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:raw_logging",
    ],
)

cc_test(
    name = "embed_file_test",
    srcs = ["embed_file_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":embed_file",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

# The main Sandboxed-API library
cc_library(
    name = "sapi",
    srcs = [
        "sandbox.cc",
        "transaction.cc",
    ],
    hdrs = [
        # TODO(hamacher): Remove reexport workaround as soon as the buildsystem
        #                 supports this usecase.
        "embed_file.h",
        "sandbox.h",
        "transaction.h",
    ],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":call",
        ":config",
        ":embed_file",
        ":var_type",
        ":vars",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:dynamic_annotations",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/log:globals",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:client",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:comms",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:util",
        "@com_google_sandboxed_api//sandboxed_api/util:file_base",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:runfiles",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
    ],
)

# Definitions shared between sandboxee and master used for higher-level IPC.
cc_library(
    name = "call",
    hdrs = ["call.h"],
    copts = sapi_platform_copts(),
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
        "rpcchannel.cc",
        "var_abstract.cc",
        "var_int.cc",
        "var_lenval.cc",
    ],
    hdrs = [
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
        ":var_type",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:span",
        "@com_google_absl//absl/utility",
        "@com_google_protobuf//:protobuf_lite",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:comms",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:util",
        "@com_google_sandboxed_api//sandboxed_api/util:proto_helper",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
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
        ":lenval_core",
        ":var_type",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:dynamic_annotations",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/log:flags",
        "@com_google_absl//absl/log:initialize",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:comms",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:forkingclient",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:logsink",
        "@com_google_sandboxed_api//sandboxed_api/util:proto_arg_cc_proto",
        "@com_google_sandboxed_api//sandboxed_api/util:proto_helper",
        "@org_sourceware_libffi//:libffi",
    ],
)

cc_test(
    name = "sapi_test",
    srcs = ["sapi_test.cc"],
    copts = sapi_platform_copts(),
    tags = ["local"],
    deps = [
        ":sapi",
        ":testing",
        ":vars",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_benchmark//:benchmark",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api/examples/stringop:stringop-sapi",
        "@com_google_sandboxed_api//sandboxed_api/examples/stringop:stringop_params_cc_proto",
        "@com_google_sandboxed_api//sandboxed_api/examples/sum:sum-sapi",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:result",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
        "@com_google_sandboxed_api//sandboxed_api/util:thread",
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
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:policybuilder",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2:testonly_allow_all_syscalls",
        "@com_google_sandboxed_api//sandboxed_api/util:file_base",
    ],
)
