# Copyright 2022 Google LLC
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

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")

licenses(["notice"])

filegroup(
    name = "testdata",
    srcs = glob(["testdata/*"]),
)

cc_library(
    name = "generator",
    srcs = [
        "diagnostics.cc",
        "emitter.cc",
        "emitter_base.cc",
        "generator.cc",
        "sandboxed_library_emitter.cc",
        "symbol_list_emitter.cc",
        "types.cc",
    ],
    hdrs = [
        "diagnostics.h",
        "emitter.h",
        "emitter_base.h",
        "generator.h",
        "includes.h",
        "sandboxed_library_emitter.h",
        "symbol_list_emitter.h",
        "types.h",
    ],
    copts = sapi_platform_copts(),
    deps = [
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:optional",
        "@llvm-project//clang:ast",
        "@llvm-project//clang:basic",
        "@llvm-project//clang:format",
        "@llvm-project//clang:frontend",
        "@llvm-project//clang:lex",
        "@llvm-project//clang:serialization",
        "@llvm-project//clang:tooling",
        "@llvm-project//clang:tooling_core",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:config",
    ],
)

cc_test(
    name = "generator_test",
    srcs = [
        "emitter_test.cc",
        "frontend_action_test_util.cc",
        "frontend_action_test_util.h",
    ],
    copts = sapi_platform_copts(),
    data = [
        ":testdata",
    ],
    deps = [
        ":generator",
        "//sandboxed_api:testing",
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:file_helpers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
        "@llvm-project//clang:basic",
        "@llvm-project//clang:frontend",
        "@llvm-project//clang:tooling",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:config",
    ],
)

# Clang tool that generates Sandboxed API headers
cc_binary(
    name = "generator_tool",
    srcs = [
        "compilation_database.cc",
        "compilation_database.h",
        "generator_tool.cc",
    ],
    copts = sapi_platform_copts(),
    visibility = ["//visibility:public"],
    deps = [
        ":generator",
        "//sandboxed_api/util:file_base",
        "//sandboxed_api/util:file_helpers",
        "//sandboxed_api/util:fileops",
        "//sandboxed_api/util:status",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@llvm-project//clang:driver",
        "@llvm-project//clang:tooling",
        "@llvm-project//llvm:Support",
        "@llvm-project//llvm:config",
    ],
)
