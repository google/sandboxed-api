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

"""Generates proto targets in various languages."""

load("@com_google_protobuf//:protobuf.bzl", "cc_proto_library")

def _cc_proto_library_name_from_proto_name(name):
    """Converts proto name to cc_proto_library name.

    Several suffixes will be considered.
    Args:
      name: the proto name
    Returns:
      The cc_proto_library name.
    """
    name = name.replace("-", "_")
    if name == "proto":
        # replace 'proto' with 'cc_proto'
        return "cc_proto"
    for suffix in [
        ".protolib",
        "protolib",
        "proto2lib",
        "proto_lib",
        "proto2",
        "protos",
        "proto2_lib",
        "libproto",
        "genproto",
        "proto",
    ]:
        # replace 'suffix' or '_suffix' with '_cc_proto'
        if name.endswith("_" + suffix):
            return name[:-len("_" + suffix)] + "_cc_proto"
        elif name.endswith(suffix):
            return name[:-len(suffix)] + "_cc_proto"

    # no match; simply append '_cc_proto' to the end
    return name + "_cc_proto"

def sapi_proto_library(
        name,
        srcs = [],
        deps = [],
        alwayslink = False,
        **kwargs):
    """Generates proto library and C++ targets.

    Args:
      name: Name for proto_library and base for the cc_proto_library name, name +
            "_cc_proto".
      srcs: Same as proto_library deps.
      deps: Same as proto_library deps.
      alwayslink: Same as cc_library.
      **kwargs: proto_library arguments.
    """
    if kwargs.get("has_services", False):
        fail("Services are not currently supported.")

    cc_proto_library(
        name = name,
        srcs = srcs,
        deps = deps,
        alwayslink = alwayslink,
        **kwargs
    )
    native.cc_library(
        name = _cc_proto_library_name_from_proto_name(name),
        deps = [name],
        alwayslink = alwayslink,
        **kwargs
    )
