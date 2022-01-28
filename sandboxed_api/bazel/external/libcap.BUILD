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

genrule(
    name = "cap_names_list_h",
    srcs = ["libcap/include/uapi/linux/capability.h"],
    outs = ["cap_names.list.h"],
    # Use the same logic as libcap/Makefile
    cmd = """
        sed -ne '/^#define[ \\t]CAP[_A-Z]\\+[ \\t]\\+[0-9]\\+/{s/^#define \\([^ \\t]*\\)[ \\t]*\\([^ \\t]*\\)/\\{"\\1",\\2\\},/p;}' $< | \\
        tr "[:upper:]" "[:lower:]" > $@
    """,
    visibility = ["//visibility:private"],
)

cc_library(
    name = "makenames_textual_hdrs",
    textual_hdrs = [
        ":cap_names.list.h",
        "libcap/include/uapi/linux/capability.h",
    ],
    visibility = ["//visibility:private"],
)

cc_binary(
    name = "makenames",
    srcs = [
        "libcap/_makenames.c",
        "libcap/include/sys/capability.h",
    ],
    includes = [
        "libcap/..",
        "libcap/include",
        "libcap/include/uapi",
    ],
    visibility = ["//visibility:private"],
    deps = [":makenames_textual_hdrs"],
)

genrule(
    name = "cap_names_h",
    outs = ["libcap/cap_names.h"],
    cmd = "mkdir -p libcap && $(location makenames) > $@ || { rm -f $@; false; }",
    tools = [":makenames"],
    visibility = ["//visibility:private"],
)

cc_library(
    name = "libcap",
    srcs = [
        "libcap/cap_alloc.c",
        "libcap/cap_extint.c",
        "libcap/cap_file.c",
        "libcap/cap_flag.c",
        "libcap/cap_proc.c",
        "libcap/cap_text.c",
    ],
    copts = [
        "-Wno-tautological-compare",
        "-Wno-unused-result",
        # Work around sys/xattr.h not declaring this
        "-DXATTR_NAME_CAPS=\"\\\"security.capability\\\"\"",
    ],
    includes = [
        "libcap",
        "libcap/include",
        "libcap/include/uapi",
    ],
    textual_hdrs = [
        "libcap/include/sys/capability.h",
        "libcap/include/uapi/linux/capability.h",
        "libcap/libcap.h",
        "libcap/cap_names.h",
    ],
    visibility = ["//visibility:public"],
)
