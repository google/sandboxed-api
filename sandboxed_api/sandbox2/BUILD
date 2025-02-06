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

load("@com_google_sandboxed_api//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("@com_google_sandboxed_api//sandboxed_api/bazel:embed_data.bzl", "sapi_cc_embed_data")
load("@com_google_sandboxed_api//sandboxed_api/bazel:proto.bzl", "sapi_proto_library")

package(default_visibility = ["@com_google_sandboxed_api//sandboxed_api:__subpackages__"])

licenses(["notice"])

cc_library(
    name = "bpfdisassembler",
    srcs = ["bpfdisassembler.cc"],
    hdrs = ["bpfdisassembler.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "bpf_evaluator",
    srcs = ["bpf_evaluator.cc"],
    hdrs = ["bpf_evaluator.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
    ],
)

cc_library(
    name = "regs",
    srcs = ["regs.cc"],
    hdrs = ["regs.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":syscall",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api:config",
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
        "@com_google_absl//absl/log:check",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:bpf_helper",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
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
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
    ],
)

cc_test(
    name = "syscall_test",
    srcs = ["syscall_test.cc"],
    copts = sapi_platform_copts(),
    tags = ["no_qemu_user_mode"],
    deps = [
        ":syscall",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:config",
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
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api:config",
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
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log",
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
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log:log_entry",
        "@com_google_absl//absl/log:log_sink",
        "@com_google_absl//absl/log:log_sink_registry",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
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
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api/util:thread",
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
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/network_proxy:filtering",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:bpf_helper",
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
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_library(
    name = "limits",
    hdrs = ["limits.h"],
    copts = sapi_platform_copts(),
    deps = [
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/time",
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
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log:globals",
        "@com_google_absl//absl/status",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/unwind",
        "@com_google_sandboxed_api//sandboxed_api/util:raw_logging",
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
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api:embed_file",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:raw_logging",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
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
        "@com_google_absl//absl/base:core_headers",
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
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
    ],
)

# Should not be used in sandboxee code if it only uses sandbox2::Comms and
# sandbox2::Client objects
cc_library(
    name = "sandbox2",
    srcs = [
        "sandbox2.cc",
    ],
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
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:map_exec",  # TODO b/371179394 - Remove this after migrating to Allow(MapExec).
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/network_proxy:client",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/network_proxy:filtering",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
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
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/unwind:unwind_cc_proto",
        "@com_google_sandboxed_api//sandboxed_api/util:file_base",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
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
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/log:vlog_is_on",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:pid_waiter",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
        "@com_google_sandboxed_api//sandboxed_api/util:thread",
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
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:span",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
        "@com_google_sandboxed_api//sandboxed_api/util:thread",
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
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/log:vlog_is_on",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/network_proxy:client",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/network_proxy:server",
        "@com_google_sandboxed_api//sandboxed_api/util:file_helpers",
        "@com_google_sandboxed_api//sandboxed_api/util:strerror",
        "@com_google_sandboxed_api//sandboxed_api/util:temp_file",
        "@com_google_sandboxed_api//sandboxed_api/util:thread",
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
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@com_google_absl//absl/types:span",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:all_syscalls",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:map_exec",  # TODO b/371179394 - Remove this after migrating to Allow(MapExec).
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:namespaces",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:seccomp_speculation",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:trace_all_syscalls",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:unrestricted_networking",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/network_proxy:filtering",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:bpf_helper",
        "@com_google_sandboxed_api//sandboxed_api/util:file_base",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
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
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/network_proxy:client",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:bpf_helper",
        "@com_google_sandboxed_api//sandboxed_api/util:raw_logging",
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
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:raw_logging",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
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
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:bpf_helper",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:raw_logging",
        "@com_google_sandboxed_api//sandboxed_api/util:strerror",
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
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/synchronization",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
    ],
)

cc_library(
    name = "mounts",
    srcs = ["mounts.cc"],
    hdrs = ["mounts.h"],
    copts = sapi_platform_copts(),
    deps = [
        ":mount_tree_cc_proto",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:minielf",
        "@com_google_sandboxed_api//sandboxed_api/util:file_base",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:raw_logging",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
    ],
)

cc_test(
    name = "mounts_test",
    srcs = ["mounts_test.cc"],
    copts = sapi_platform_copts(),
    data = ["@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:minimal_dynamic"],
    deps = [
        ":mount_tree_cc_proto",
        ":mounts",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/util:file_base",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
        "@com_google_sandboxed_api//sandboxed_api/util:temp_file",
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
        "@com_google_absl//absl/strings",
        "@com_google_sandboxed_api//sandboxed_api/util:file_base",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:raw_logging",
    ],
)

cc_test(
    name = "namespace_test",
    srcs = ["namespace_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:namespace",
    ],
    tags = [
        "requires-net:external",
    ],
    deps = [
        ":namespace",
        ":sandbox2",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:namespaces",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:testonly_all_syscalls",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:testonly_unrestricted_networking",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
        "@com_google_sandboxed_api//sandboxed_api/util:temp_file",
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
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
    ],
)

cc_library(
    name = "util",
    srcs = ["util.cc"],
    hdrs = ["util.h"],
    # The default is 16384, however we need to do a clone with a
    # stack-allocated buffer -- and PTHREAD_STACK_MIN also happens to be 16384.
    # Thus the slight increase.
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_absl//absl/algorithm:container",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/types:span",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api/util:file_base",
        "@com_google_sandboxed_api//sandboxed_api/util:file_helpers",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:raw_logging",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
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
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

cc_test(
    name = "buffer_test",
    srcs = ["buffer_test.cc"],
    copts = sapi_platform_copts(),
    data = ["@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:buffer"],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":buffer",
        ":sandbox2",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
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
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/base:dynamic_annotations",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:raw_logging",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
        "@com_google_sandboxed_api//sandboxed_api/util:status_cc_proto",
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
        "@com_google_absl//absl/container:fixed_array",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
        "@com_google_sandboxed_api//sandboxed_api/util:thread",
    ],
)

cc_test(
    name = "forkserver_test",
    srcs = ["forkserver_test.cc"],
    copts = sapi_platform_copts(),
    data = ["@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:minimal"],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":forkserver",
        ":forkserver_cc_proto",
        ":global_forkserver",
        ":sandbox2",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:testing",
    ],
)

cc_test(
    name = "limits_test",
    srcs = ["limits_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:limits",
    ],
    deps = [
        ":limits",
        ":sandbox2",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
    ],
)

cc_test(
    name = "notify_test",
    srcs = ["notify_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:minimal",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:personality",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:pidcomms",
    ],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":comms",
        ":sandbox2",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:trace_all_syscalls",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
    ],
)

cc_test(
    name = "policy_test",
    srcs = ["policy_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:add_policy_on_syscalls",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:malloc_system",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:minimal",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:minimal_dynamic",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:policy",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:posix_timers",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:sandbox_detection",
    ],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":sandbox2",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:bpf_helper",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
    ],
)

cc_test(
    name = "sandbox2_test",
    srcs = ["sandbox2_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:abort",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:custom_fork",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:minimal",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:sleep",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:starve",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:tsync",
    ],
    tags = [
        "local",
        "no_qemu_user_mode",
    ],
    deps = [
        ":fork_client",
        ":sandbox2",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:config",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
        "@com_google_sandboxed_api//sandboxed_api/util:thread",
    ],
)

cc_test(
    name = "sanitizer_test",
    srcs = ["sanitizer_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:close_fds",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:sanitizer",
    ],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":comms",
        ":sandbox2",
        ":sanitizer",
        ":util",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
    ],
)

cc_test(
    name = "util_test",
    srcs = ["util_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:util_communicate",
    ],
    deps = [
        ":util",
        "@com_google_absl//absl/cleanup",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
    ],
)

cc_test(
    name = "stack_trace_test",
    srcs = [
        "stack_trace_test.cc",
    ],
    copts = sapi_platform_copts(),
    data = ["@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:symbolize"],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":global_forkserver",
        ":sandbox2",
        ":stack_trace",
        "@com_google_absl//absl/base:log_severity",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/log:scoped_mock_log",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:testonly_all_syscalls",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
    ],
)

cc_test(
    name = "ipc_test",
    srcs = ["ipc_test.cc"],
    copts = sapi_platform_copts(),
    data = ["@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:ipc"],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":comms",
        ":sandbox2",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
    ],
)

cc_library(
    name = "testing",
    testonly = 1,
    hdrs = ["testing.h"],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = ["@com_google_sandboxed_api//sandboxed_api:testing"],
)

cc_test(
    name = "policybuilder_test",
    srcs = ["policybuilder_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":policy",
        ":policybuilder",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/allowlists:unrestricted_networking",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:bpf_helper",
        "@com_google_sandboxed_api//sandboxed_api/util:file_base",
        "@com_google_sandboxed_api//sandboxed_api/util:fileops",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
    ],
)

cc_test(
    name = "bpfdisassembler_test",
    srcs = ["bpfdisassembler_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":bpfdisassembler",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:bpf_helper",
    ],
)

cc_test(
    name = "bpf_evaluator_test",
    srcs = ["bpf_evaluator_test.cc"],
    copts = sapi_platform_copts(),
    deps = [
        ":bpf_evaluator",
        "@com_google_absl//absl/status",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/util:bpf_helper",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
    ],
)

cc_test(
    name = "network_proxy_test",
    srcs = ["network_proxy_test.cc"],
    copts = sapi_platform_copts(),
    data = [
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/testcases:network_proxy",
    ],
    tags = ["no_qemu_user_mode"],
    deps = [
        ":sandbox2",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest_main",
        "@com_google_sandboxed_api//sandboxed_api:testing",
        "@com_google_sandboxed_api//sandboxed_api/sandbox2/network_proxy:testing",
        "@com_google_sandboxed_api//sandboxed_api/util:status_matchers",
    ],
)
