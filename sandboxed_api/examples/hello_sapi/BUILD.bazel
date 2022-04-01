# Copyright 2020 Google LLC
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

load(
    "@com_google_sandboxed_api//sandboxed_api/bazel:sapi.bzl",
    "sapi_library",
)

# Library with code that should be sandboxed
cc_library(
    name = "hello_lib",
    srcs = ["hello_lib.cc"],
    alwayslink = 1,
)

# Sandboxed API for the library above
sapi_library(
    name = "hello_sapi",
    functions = [
        "AddTwoIntegers",
    ],
    generator_version = 1,
    input_files = ["hello_lib.cc"],
    lib = ":hello_lib",
    lib_name = "Hello",
    visibility = ["//visibility:public"],
)

# Main executable demonstrating how the sandboxed library is used
cc_binary(
    name = "hello",
    srcs = ["hello_main.cc"],
    deps = [":hello_sapi"],
)

# Another example using the same library, but using the Transaction API that
# automatically retries sandbox operations. Also demonstates error handling
# and a custom security policy.
cc_binary(
    name = "hello_transacted",
    srcs = ["hello_transacted.cc"],
    deps = [
        ":hello_sapi",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_sandboxed_api//sandboxed_api/util:status",
    ],
)
