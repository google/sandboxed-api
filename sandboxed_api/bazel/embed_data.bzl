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

"""Embeds binary data in cc_*() rules for open-source builds."""

load("@rules_cc//cc:defs.bzl", "cc_library")

_FILEWRAPPER = "//sandboxed_api/tools/filewrapper:filewrapper"

def _sapi_cc_embed_data_impl(ctx):
    cc_file_artifact = None
    h_file_artifact = None
    s_file_artifact = None
    for output in ctx.outputs.outs:
        if output.path.endswith(".h"):
            h_file_artifact = output
        elif output.path.endswith(".cc") or output.path.endswith(".cpp"):
            cc_file_artifact = output
        elif output.path.endswith(".S") or output.path.endswith(".s"):
            s_file_artifact = output

    args = ctx.actions.args()
    args.add(ctx.label.package)
    args.add(ctx.attr.ident)
    args.add(ctx.attr.namespace if ctx.attr.namespace else "")
    args.add(h_file_artifact)
    args.add(cc_file_artifact)
    args.add(s_file_artifact)
    args.add_all(ctx.files.srcs)

    ctx.actions.run(
        executable = ctx.executable._filewrapper,
        inputs = ctx.files.srcs,
        outputs = [h_file_artifact, cc_file_artifact, s_file_artifact],
        arguments = [args],
        mnemonic = "CcEmbedData",
        progress_message = (
            "Creating sapi_cc_embed_data file for {}".format(ctx.label)
        ),
    )

_sapi_cc_embed_data = rule(
    implementation = _sapi_cc_embed_data_impl,
    attrs = {
        "srcs": attr.label_list(
            allow_files = True,
        ),
        "namespace": attr.string(),
        "ident": attr.string(),
        "_filewrapper": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label(_FILEWRAPPER),
        ),
        "outs": attr.output_list(),
    },
)

def sapi_cc_embed_data(
        name,
        srcs = [],
        namespace = "",
        tags = [],
        visibility = None,
        **kwargs):
    """Embeds arbitrary binary data in cc_*() rules.

    Args:
      name: Name for this rule.
      srcs: A list of files to be embedded.
      namespace: C++ namespace to wrap the generated types in.
      tags: Standard target attribute.
      visibility: Standard target attribute.
      **kwargs: extra arguments like testonly, visibility, etc.
    """
    common = _common_kwargs(tags, visibility)
    embed_name = name.replace("-", "_").replace(".", "_")

    _sapi_cc_embed_data(
        name = "_%s_sapi" % name,
        srcs = srcs,
        namespace = namespace,
        ident = embed_name,
        outs = [
            "%s.h" % embed_name,
            "%s.cc" % embed_name,
            "%s.S" % embed_name,
        ],
        **common
    )

    hdrs = [":%s.h" % embed_name]
    if name != embed_name:
        native.genrule(
            name = "_%s_h_sapi" % embed_name,
            srcs = [":%s.h" % embed_name],
            outs = ["%s.h" % name],
            cmd = "cp $< $@",
            **common
        )
        hdrs = hdrs + [":%s.h" % name]

    cc_library(
        name = name,
        hdrs = hdrs,
        srcs = [
            ":%s.cc" % embed_name,
            ":%s.S" % embed_name,
        ],
        additional_compiler_inputs = srcs,
        deps = [
            "@abseil-cpp//absl/base:core_headers",
            "@abseil-cpp//absl/strings",
            "//sandboxed_api:embed_toc",
        ],
        **(kwargs | common)
    )

def _common_kwargs(tags, visibility):
    common = {
        "tags": tags,
    }
    if visibility:
        common["visibility"] = visibility
    return common
