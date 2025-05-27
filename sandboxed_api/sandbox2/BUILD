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

# Description: sandbox2 is a C++ sandbox technology for Linux.

load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("//sandboxed_api/bazel:embed_data.bzl", "sapi_cc_embed_data")
load("//sandboxed_api/bazel:proto.bzl", "sapi_proto_library")

package(default_visibility = ["//sandboxed_api:__subpackages__"])

licenses(["notice"])

cc_library(
    name = "bpfdisassembler",
    srcs = ["bpfdisassembler.cc"],
    hdrs = ["bpfdisassembler.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "bpf_evaluator",
    srcs = ["bpf_evaluator.cc"],
    hdrs = ["bpf_evaluator.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "regs",
    srcs = ["regs.cc"],
    hdrs = ["regs.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":syscall",
        "//sandboxed_api:config",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "regs_test",
    srcs = ["regs_test.cc"],
    copts = sapi_platform_copts(),
    tags = ["no_qemu_user_mode"],
    deps = [
        ":regs",
        ":sanitizer",
        ":syscall",
        ":util",
        "//sandboxed_api:config",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "//sandboxed_api/util:status_matchers",
        "@abseil-cpp//absl/log:check",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "syscall",
    srcs = [
        "syscall.cc",
        "syscall_defs.cc",
    ],
    hdrs = [
        "syscall.h",
        "syscall_defs.h",
    ],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":util",
        "//sandboxed_api:config",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "syscall_test",
    srcs = ["syscall_test.cc"],
    copts = sapi_platform_copts(),
    tags = ["no_qemu_user_mode"],
    deps = [
        ":syscall",
        "//sandboxed_api:config",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "result",
    srcs = ["result.cc"],
    hdrs = ["result.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":regs",
        ":syscall",
        ":util",
        "//sandboxed_api:config",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

sapi_proto_library(
    name = "logserver_proto",
    srcs = ["logserver.proto"],
)

cc_library(
    name = "logserver",
    srcs = ["logserver.cc"],
    hdrs = ["logserver.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":comms",
        ":logserver_cc_proto",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "logsink",
    srcs = ["logsink.cc"],
    hdrs = ["logsink.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":comms",
        ":logserver_cc_proto",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log:log_sink",
        "@abseil-cpp//absl/log:log_sink_registry",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "ipc",
    srcs = ["ipc.cc"],
    hdrs = ["ipc.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":comms",
        ":logserver",
        ":logsink",
        "//sandboxed_api/util:thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "policy",
    srcs = ["policy.cc"],
    hdrs = ["policy.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":bpfdisassembler",
        ":namespace",
        ":syscall",
        ":util",
        "//sandboxed_api:config",
        "//sandboxed_api/sandbox2/network_proxy:filtering",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "//sandboxed_api/sandbox2/util:seccomp_unotify",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "notify",
    srcs = [],
    hdrs = ["notify.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":comms",
        ":result",
        ":syscall",
        ":util",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "limits",
    hdrs = ["limits.h"],
    copts = sapi_platform_copts(),
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/time",
    ],
)

cc_binary(
    name = "forkserver_bin",
    srcs = ["forkserver_bin.cc"],
    copts = sapi_platform_copts(),
    stamp = 0,
    deps = [
        ":client",
        ":comms",
        ":forkserver",
        ":sanitizer",
        "//sandboxed_api/sandbox2/unwind",
        "//sandboxed_api/util:raw_logging",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log:globals",
        "@abseil-cpp//absl/status",
    ],
)

sapi_cc_embed_data(
    name = "forkserver_bin_embed",
    srcs = [":forkserver_bin.stripped"],
)

cc_library(
    name = "global_forkserver",
    srcs = ["global_forkclient.cc"],
    hdrs = ["global_forkclient.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":comms",
        ":fork_client",
        ":forkserver_bin_embed",
        ":forkserver_cc_proto",
        ":util",
        "//sandboxed_api:config",
        "//sandboxed_api:embed_file",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:raw_logging",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
    ],
)

# Use only if Sandbox2 global forkserver has to be started very early on.
# By default the forkserver is started on demand.
cc_library(
    name = "start_global_forkserver_lib_constructor",
    srcs = ["global_forkclient_lib_ctor.cc"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":fork_client",
        ":global_forkserver",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "executor",
    srcs = ["executor.cc"],
    hdrs = ["executor.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":fork_client",
        ":forkserver_cc_proto",
        ":global_forkserver",
        ":ipc",
        ":limits",
        ":namespace",
        ":util",
        "//sandboxed_api:config",
        "//sandboxed_api/util:fileops",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

# Should not be used in sandboxee code if it only uses sandbox2::Comms and
# sandbox2::Client objects
cc_library(
    name = "sandbox2",
    srcs = ["sandbox2.cc"],
    hdrs = [
        "client.h",
        "executor.h",
        "ipc.h",
        "limits.h",
        "notify.h",
        "policy.h",
        "policybuilder.h",
        "result.h",
        "sandbox2.h",
        "syscall.h",
    ],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":client",
        ":comms",
        ":executor",
        ":fork_client",
        ":forkserver_cc_proto",
        ":ipc",
        ":limits",
        ":logsink",
        ":monitor_base",
        ":monitor_ptrace",
        ":monitor_unotify",
        ":mounts",
        ":namespace",
        ":notify",
        ":policy",
        ":policybuilder",
        ":regs",
        ":result",
        ":stack_trace",
        ":syscall",
        ":util",
        "//sandboxed_api:config",
        "//sandboxed_api/sandbox2/allowlists:map_exec",  # TODO: b/371179394 - Remove this after migrating to Allow(MapExec).
        "//sandboxed_api/sandbox2/network_proxy:client",
        "//sandboxed_api/sandbox2/network_proxy:filtering",
        "//sandboxed_api/util:fileops",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "stack_trace",
    srcs = ["stack_trace.cc"],
    hdrs = ["stack_trace.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":comms",
        ":executor",
        ":limits",
        ":mounts",
        ":namespace",
        ":policy",
        ":policybuilder",
        ":regs",
        ":result",
        "//sandboxed_api/sandbox2/unwind:unwind_cc_proto",
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "monitor_ptrace",
    srcs = ["monitor_ptrace.cc"],
    hdrs = ["monitor_ptrace.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":client",
        ":comms",
        ":executor",
        ":monitor_base",
        ":notify",
        ":policy",
        ":regs",
        ":result",
        ":sanitizer",
        ":syscall",
        ":util",
        "//sandboxed_api:config",
        "//sandboxed_api/sandbox2/util:pid_waiter",
        "//sandboxed_api/util:status",
        "//sandboxed_api/util:thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:vlog_is_on",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "monitor_unotify",
    srcs = ["monitor_unotify.cc"],
    hdrs = ["monitor_unotify.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":bpf_evaluator",
        ":client",
        ":executor",
        ":forkserver_cc_proto",
        ":monitor_base",
        ":notify",
        ":policy",
        ":result",
        ":util",
        "//sandboxed_api/sandbox2/util:seccomp_unotify",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:status",
        "//sandboxed_api/util:thread",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "monitor_base",
    srcs = ["monitor_base.cc"],
    hdrs = ["monitor_base.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":client",
        ":comms",
        ":executor",
        ":fork_client",
        ":forkserver_cc_proto",
        ":ipc",
        ":limits",
        ":mounts",
        ":namespace",
        ":notify",
        ":policy",
        ":regs",
        ":result",
        ":stack_trace",
        ":syscall",
        ":util",
        "//sandboxed_api/sandbox2/network_proxy:client",
        "//sandboxed_api/sandbox2/network_proxy:server",
        "//sandboxed_api/util:file_helpers",
        "//sandboxed_api/util:strerror",
        "//sandboxed_api/util:temp_file",
        "//sandboxed_api/util:thread",
        "@abseil-cpp//absl/base",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:vlog_is_on",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "policybuilder",
    srcs = ["policybuilder.cc"],
    hdrs = ["policybuilder.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":forkserver_cc_proto",
        ":mounts",
        ":namespace",
        ":policy",
        ":syscall",
        "//sandboxed_api:config",
        "//sandboxed_api/sandbox2/allowlists:all_syscalls",
        "//sandboxed_api/sandbox2/allowlists:map_exec",  # TODO: b/371179394 - Remove this after migrating to Allow(MapExec).
        "//sandboxed_api/sandbox2/allowlists:namespaces",
        "//sandboxed_api/sandbox2/allowlists:seccomp_speculation",
        "//sandboxed_api/sandbox2/allowlists:trace_all_syscalls",
        "//sandboxed_api/sandbox2/allowlists:unrestricted_networking",
        "//sandboxed_api/sandbox2/network_proxy:filtering",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
        "@abseil-cpp//absl/types:span",
    ],
)

# Should be used in sandboxee code instead of :sandbox2 if it uses just
# sandbox2::Client::SandboxMeHere() and sandbox2::Comms
cc_library(
    name = "client",
    srcs = ["client.cc"],
    hdrs = ["client.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":comms",
        ":logsink",
        ":policy",
        ":sanitizer",
        ":syscall",
        "//sandboxed_api/sandbox2/network_proxy:client",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "//sandboxed_api/util:raw_logging",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "sanitizer",
    srcs = ["sanitizer.cc"],
    hdrs = ["sanitizer.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":util",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:raw_logging",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "forkserver",
    srcs = ["forkserver.cc"],
    hdrs = ["forkserver.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":client",
        ":comms",
        ":fork_client",
        ":forkserver_cc_proto",
        ":namespace",
        ":policy",
        ":sanitizer",
        ":syscall",
        ":util",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:raw_logging",
        "//sandboxed_api/util:strerror",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@org_kernel_libcap//:libcap",
    ],
)

cc_library(
    name = "fork_client",
    srcs = ["fork_client.cc"],
    hdrs = ["fork_client.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":comms",
        ":forkserver_cc_proto",
        "//sandboxed_api/util:fileops",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "mounts",
    srcs = ["mounts.cc"],
    hdrs = ["mounts.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":mount_tree_cc_proto",
        "//sandboxed_api:config",
        "//sandboxed_api/sandbox2/util:minielf",
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:raw_logging",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "mounts_test",
    srcs = ["mounts_test.cc"],
    copts = sapi_platform_copts(),
    data = ["//sandboxed_api/sandbox2/testcases:minimal_dynamic"],
    deps = [
        ":mount_tree_cc_proto",
        ":mounts",
        "//sandboxed_api:testing",
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:status_matchers",
        "//sandboxed_api/util:temp_file",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "namespace",
    srcs = ["namespace.cc"],
    hdrs = ["namespace.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":forkserver_cc_proto",
        ":mounts",
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:raw_logging",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "namespace_test",
    srcs = ["namespace_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "//sandboxed_api/sandbox2/testcases:namespace",
    ],
    tags = [
        "requires-net:external",
    ],
    deps = [
        ":namespace",
        ":sandbox2",
        "//sandboxed_api:testing",
        "//sandboxed_api/sandbox2/allowlists:namespaces",
        "//sandboxed_api/sandbox2/allowlists:testonly_all_syscalls",
        "//sandboxed_api/sandbox2/allowlists:testonly_unrestricted_networking",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:status_matchers",
        "//sandboxed_api/util:temp_file",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "forkingclient",
    srcs = ["forkingclient.cc"],
    hdrs = ["forkingclient.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":client",
        ":comms",
        ":forkserver",
        ":sanitizer",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "util",
    srcs = ["util.cc"],
    hdrs = ["util.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        "//sandboxed_api:config",
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:file_helpers",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:raw_logging",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:span",
    ],
)

# Library for C-wrappers of util.h.
cc_library(
    name = "util_c",
    srcs = ["util_c.cc"],
    hdrs = ["util_c.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":util",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "buffer",
    srcs = ["buffer.cc"],
    hdrs = ["buffer.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":util",
        "//sandboxed_api/util:fileops",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "buffer_test",
    srcs = ["buffer_test.cc"],
    copts = sapi_platform_copts(),
    data = ["//sandboxed_api/sandbox2/testcases:buffer"],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":buffer",
        ":sandbox2",
        "//sandboxed_api:testing",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:status_matchers",
        "@googletest//:gtest_main",
    ],
)

sapi_proto_library(
    name = "forkserver_proto",
    srcs = ["forkserver.proto"],
    deps = [":mount_tree_proto"],
)

sapi_proto_library(
    name = "mount_tree_proto",
    srcs = ["mount_tree.proto"],
)

cc_library(
    name = "comms",
    srcs = ["comms.cc"],
    hdrs = ["comms.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":util",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:raw_logging",
        "//sandboxed_api/util:status",
        "//sandboxed_api/util:status_cc_proto",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:dynamic_annotations",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
    ],
)

sapi_proto_library(
    name = "comms_test_proto",
    srcs = ["comms_test.proto"],
)

cc_test(
    name = "comms_test",
    srcs = ["comms_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":comms",
        ":comms_test_cc_proto",
        "//sandboxed_api/util:status_matchers",
        "//sandboxed_api/util:thread",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "forkserver_test",
    srcs = ["forkserver_test.cc"],
    copts = sapi_platform_copts(),
    data = ["//sandboxed_api/sandbox2/testcases:minimal"],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":forkserver",
        ":forkserver_cc_proto",
        ":global_forkserver",
        ":sandbox2",
        "//sandboxed_api:testing",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "limits_test",
    srcs = ["limits_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "//sandboxed_api/sandbox2/testcases:limits",
    ],
    deps = [
        ":limits",
        ":sandbox2",
        "//sandboxed_api:config",
        "//sandboxed_api:testing",
        "//sandboxed_api/util:status_matchers",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "notify_test",
    srcs = ["notify_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "//sandboxed_api/sandbox2/testcases:minimal",
        "//sandboxed_api/sandbox2/testcases:personality",
        "//sandboxed_api/sandbox2/testcases:pidcomms",
    ],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":comms",
        ":sandbox2",
        "//sandboxed_api:testing",
        "//sandboxed_api/sandbox2/allowlists:trace_all_syscalls",
        "//sandboxed_api/util:status_matchers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "policy_test",
    srcs = ["policy_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "//sandboxed_api/sandbox2/testcases:add_policy_on_syscalls",
        "//sandboxed_api/sandbox2/testcases:execveat",
        "//sandboxed_api/sandbox2/testcases:malloc_system",
        "//sandboxed_api/sandbox2/testcases:minimal",
        "//sandboxed_api/sandbox2/testcases:minimal_dynamic",
        "//sandboxed_api/sandbox2/testcases:policy",
        "//sandboxed_api/sandbox2/testcases:posix_timers",
        "//sandboxed_api/sandbox2/testcases:sandbox_detection",
    ],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":sandbox2",
        "//sandboxed_api:config",
        "//sandboxed_api:testing",
        "//sandboxed_api/sandbox2/allowlists:seccomp_speculation",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:status_matchers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sandbox2_test",
    srcs = ["sandbox2_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "//sandboxed_api/sandbox2/testcases:abort",
        "//sandboxed_api/sandbox2/testcases:custom_fork",
        "//sandboxed_api/sandbox2/testcases:minimal",
        "//sandboxed_api/sandbox2/testcases:sleep",
        "//sandboxed_api/sandbox2/testcases:starve",
        "//sandboxed_api/sandbox2/testcases:tsync",
    ],
    tags = [
        "local",
        "no_qemu_user_mode",
    ],
    deps = [
        ":fork_client",
        ":sandbox2",
        "//sandboxed_api:config",
        "//sandboxed_api:testing",
        "//sandboxed_api/util:status_matchers",
        "//sandboxed_api/util:thread",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sanitizer_test",
    srcs = ["sanitizer_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "//sandboxed_api/sandbox2/testcases:close_fds",
        "//sandboxed_api/sandbox2/testcases:sanitizer",
    ],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":comms",
        ":sandbox2",
        ":sanitizer",
        ":util",
        "//sandboxed_api:testing",
        "//sandboxed_api/util:status_matchers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "util_test",
    srcs = ["util_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "//sandboxed_api/sandbox2/testcases:util_communicate",
    ],
    deps = [
        ":util",
        "//sandboxed_api:testing",
        "//sandboxed_api/util:status_matchers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "stack_trace_test",
    srcs = [
        "stack_trace_test.cc",
    ],
    copts = sapi_platform_copts(),
    data = ["//sandboxed_api/sandbox2/testcases:symbolize"],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":global_forkserver",
        ":sandbox2",
        ":stack_trace",
        "//sandboxed_api:testing",
        "//sandboxed_api/sandbox2/allowlists:testonly_all_syscalls",
        "//sandboxed_api/sandbox2/allowlists:testonly_namespaces",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:status_matchers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:scoped_mock_log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "ipc_test",
    srcs = ["ipc_test.cc"],
    copts = sapi_platform_copts(),
    data = ["//sandboxed_api/sandbox2/testcases:ipc"],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":comms",
        ":sandbox2",
        "//sandboxed_api:testing",
        "//sandboxed_api/util:status_matchers",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "testing",
    testonly = 1,
    hdrs = ["testing.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = ["//sandboxed_api:testing"],
)

cc_test(
    name = "policybuilder_test",
    srcs = ["policybuilder_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":policy",
        ":policybuilder",
        "//sandboxed_api/sandbox2/allowlists:unrestricted_networking",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:status_matchers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "bpfdisassembler_test",
    srcs = ["bpfdisassembler_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":bpfdisassembler",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "bpf_evaluator_test",
    srcs = ["bpf_evaluator_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":bpf_evaluator",
        "//sandboxed_api/sandbox2/util:bpf_helper",
        "//sandboxed_api/util:status_matchers",
        "@abseil-cpp//absl/status",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "network_proxy_test",
    srcs = ["network_proxy_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "//sandboxed_api/sandbox2/testcases:network_proxy",
    ],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":sandbox2",
        "//sandboxed_api:testing",
        "//sandboxed_api/sandbox2/network_proxy:testing",
        "//sandboxed_api/util:status_matchers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)
