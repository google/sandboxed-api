# Copyright 2019 Google LLC. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Generates proto targets in various languages."""

load(
    "@com_google_protobuf//:protobuf.bzl",
    "cc_proto_library",
    "py_proto_library",
)

def sapi_proto_library(
        name,
        srcs = [],
        deps = [],
        alwayslink = False,
        **kwargs):
    """Generates proto targets in various languages.

    Args:
      name: Name for proto_library and base for the cc_proto_library name, name +
            "_cc".
      srcs: Same as proto_library deps.
      deps: Same as proto_library deps.
      alwayslink: Same as cc_library.
      **kwargs: proto_library arguments.
    """
    if kwargs.get("has_services", False):
        fail("Services are not currently supported.")

    # TODO(cblichmann): For the time being, rely on the non-native rule
    #                   provided by Protobuf. Once the Starlark C++ API has
    #                   landed, it'll become possible to implement alwayslink
    #                   natively.
    cc_proto_library(
        name = name,
        srcs = srcs,
        deps = deps,
        alwayslink = alwayslink,
        **kwargs
    )
    native.cc_library(
        name = name + "_cc",
        deps = [":" + name],
        **kwargs
    )

def sapi_py_proto_library(name, srcs = [], deps = [], **kwargs):
    """Generates proto targets for Python.

    Args:
      name: Name for proto_library.
      srcs: Same as py_proto_library deps.
      deps: Ignored, provided for compatibility only.
      **kwargs: proto_library arguments.
    """
    _ignore = [deps]
    py_proto_library(
        name = name,
        srcs = srcs,
        default_runtime = "@com_google_protobuf//:protobuf_python",
        protoc = "@com_google_protobuf//:protoc",
        **kwargs
    )
