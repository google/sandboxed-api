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
load("@com_google_sandboxed_api//sandboxed_api/bazel:sapi.bzl", "sapi_library")

licenses(["notice"])

py_library(
    name = "code",
    srcs = ["code.py"],
)

py_test(
    name = "code_test",
    size = "small",
    srcs = [
        "code_test.py",
        "code_test_util.py",
    ],
    deps = [
        ":code",
        "@com_google_absl_py//absl/testing:absltest",
        "@com_google_absl_py//absl/testing:parameterized",
    ],
)

py_binary(
    name = "sapi_generator",
    srcs = ["sapi_generator.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":code",
        "@com_google_absl_py//absl:app",
        "@com_google_absl_py//absl/flags",
        "@com_google_absl_py//absl/logging",
    ],
)

cc_library(
    name = "tests",
    srcs = [
        "testdata/tests.cc",
        "testdata/tests.h",
        "testdata/tests2.cc",
    ],
    copts = sapi_platform_copts(),
    alwayslink = 1,
)

# Targets for testing if generated code compiles
sapi_library(
    name = "tests_sapi_generator",
    generator_version = 1,
    input_files = [
        "testdata/tests.cc",
        "testdata/tests2.cc",
    ],
    lib = ":tests",
    lib_name = "Tests",
    namespace = "sapi::tests",
)

cc_binary(
    name = "build_test_bin",
    srcs = [
        "testdata/main.cc",
        ":tests_sapi_generator.sapi.h",
        ":tests_sapi_generator_embed.h",
    ],
    copts = sapi_platform_copts(),
    deps = [
        ":tests_sapi_generator",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
        "@com_google_sandboxed_api//sandboxed_api:sapi",
        "@com_google_sandboxed_api//sandboxed_api:vars",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
    ],
)

# Compilation test - always passes if dependency builds properly
sh_test(
    name = "build_test",
    srcs = ["build_test.sh"],
    data = [":build_test_bin"],
    tags = ["nobuilder"],
)
