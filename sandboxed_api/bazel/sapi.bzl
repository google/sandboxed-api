# Copyright 2019 Google LLC
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

"""Macros that simplifies header and library generation for Sandboxed API."""

load("//sandboxed_api/bazel:embed_data.bzl", "sapi_cc_embed_data")

# Helper functions
def append_arg(arguments, name, value):
    if value:
        arguments.append("{}".format(name))
        arguments.append(value)

def append_all(arguments, name, values):
    if values:
        for v in values:
            append_arg(arguments, name, v)

def get_embed_dir():
    return native.package_name()

def sort_deps(deps):
    colon_deps = [x for x in deps if x.startswith(":")]
    other_deps = [x for x in deps if not x.startswith(":")]
    return sorted(colon_deps) + sorted(other_deps)

def sapi_interface_impl(ctx):
    """Implementation of build rule that generates SAPI interface."""

    # TODO(szwl): warn if input_files is not set and we didn't find anything
    input_files_paths = []
    input_files = []

    args = []
    append_arg(args, "--sapi_name", ctx.attr.lib_name)
    append_arg(args, "--sapi_out", ctx.outputs.out.path)
    append_arg(args, "--sapi_embed_dir", ctx.attr.embed_dir)
    append_arg(args, "--sapi_embed_name", ctx.attr.embed_name)
    append_arg(args, "--sapi_functions", ",".join(ctx.attr.functions))
    append_arg(args, "--sapi_ns", ctx.attr.namespace)
    if ctx.attr.isystem:
        isystem = ctx.attr.isystem.files.to_list()[0]
        append_arg(args, "--sapi_isystem", isystem.path)
        input_files += [isystem]

    # Parse provided files.

    # The parser doesn't need the entire set of transitive headers
    # here, just the top-level cc_library headers. It would be nice
    # if Skylark or Bazel provided this, but it is surprisingly hard
    # to get.
    #
    # Allow all headers through that contain the dependency's
    # package path. Including extra headers is harmless except that
    # we may hit Bazel's file-count limit, so be conservative and
    # pass a lot through that we don't strictly need.

    extra_flags = []
    if ctx.attr.lib[CcInfo]:
        cc_ctx = ctx.attr.lib[CcInfo].compilation_context

        # Append system headers as dependencies
        input_files += cc_ctx.headers.to_list()
        append_all(extra_flags, "-D", cc_ctx.defines.to_list())
        append_all(extra_flags, "-isystem", cc_ctx.system_includes.to_list())
        append_all(extra_flags, "-iquote", cc_ctx.quote_includes.to_list())

        if ctx.attr.input_files:
            for h in cc_ctx.headers.to_list():
                # Collect all headers as dependency in case libclang needs them.
                if h.extension == "h" and "/PROTECTED/" not in h.path:
                    input_files.append(h)
            for target in ctx.attr.input_files:
                if target.files:
                    for f in target.files.to_list():
                        input_files_paths.append(f.path)
                        input_files.append(f)

            # Try to find files automatically.
        else:
            for h in cc_ctx.headers.to_list():
                # Collect all headers as dependency in case clang needs them.
                if h.extension == "h" and "/PROTECTED/" not in h.path:
                    input_files.append(h)

                    # Include only headers coming from the target
                    # not ones that it depends on by comparing the label packages.
                    if (h.owner.package == ctx.attr.lib.label.package):
                        input_files_paths.append(h.path)

        append_arg(args, "--sapi_in", ",".join(input_files_paths))
        args += ["--"] + extra_flags
    else:
        # TODO(szwl): Error out if the lib has no cc.
        pass

    progress_msg = ("Generating {} from {} header files." +
                    "").format(ctx.outputs.out.short_path, len(input_files_paths))
    ctx.actions.run(
        inputs = input_files,
        outputs = [ctx.outputs.out],
        arguments = args,
        progress_message = progress_msg,
        executable = ctx.executable._sapi_generator,
    )

# Build rule that generates SAPI interface.
sapi_interface = rule(
    implementation = sapi_interface_impl,
    attrs = {
        "out": attr.output(mandatory = True),
        "embed_dir": attr.string(),
        "embed_name": attr.string(),
        "functions": attr.string_list(allow_empty = True, default = []),
        "include_prefix": attr.string(),
        "input_files": attr.label_list(allow_files = True),
        "lib": attr.label(mandatory = True),
        "lib_name": attr.string(mandatory = True),
        "namespace": attr.string(),
        "isystem": attr.label(),
        "_sapi_generator": attr.label(
            executable = True,
            cfg = "host",
            allow_files = True,
            default = Label("@com_google_sandboxed_api//sandboxed_api/" +
                            "tools/generator2:sapi_generator"),
        ),
    },
    output_to_genfiles = True,
)

def sapi_library(
        name,
        lib,
        lib_name,
        namespace = "",
        embed = True,
        add_default_deps = True,
        srcs = [],
        hdrs = [],
        functions = [],
        header = "",
        input_files = [],
        deps = [],
        tags = [],
        visibility = None):
    """BUILD rule providing implementation of a Sandboxed API library."""

    repo_name = native.repository_name()
    rprefix = "@com_google_sandboxed_api" if repo_name != "@" else ""
    common = {
        "tags": tags,
    }
    if visibility:
        common["visibility"] = visibility

    generated_header = name + ".sapi.h"

    # Reference (pull into the archive) required functions only. If the functions'
    # array is empty, pull in the whole archive (may not compile with MSAN).
    exported_funcs = ["-Wl,--export-dynamic-symbol," + s for s in functions]
    if (not exported_funcs):
        exported_funcs = [
            "-Wl,--whole-archive",
            "-Wl,--allow-multiple-definition",
        ]

    lib_hdrs = hdrs or []
    if header:
        lib_hdrs += [header]
    else:
        lib_hdrs += [generated_header]

    default_deps = [rprefix + "//sandboxed_api/sandbox2"]

    # Library that contains generated interface and sandboxed binary as a data
    # dependency. Add this as a dependency instead of original library.
    native.cc_library(
        name = name,
        srcs = srcs,
        hdrs = lib_hdrs,
        data = [":" + name + ".bin"],
        deps = sort_deps(
            [
                rprefix + "//sandboxed_api:sapi",
                rprefix + "//sandboxed_api:vars",
            ] + deps +
            ([":" + name + "_embed"] if embed else []) +
            (default_deps if add_default_deps else []),
        ),
        **common
    )

    native.cc_binary(
        name = name + ".bin",
        linkopts = [
            "-ldl",  # For dlopen(), dlsym()
            # The sandboxing client must have access to all symbols used in
            # the sandboxed library, so these must be both referenced, and
            # exported
            "-Wl,-E",
        ] + exported_funcs,
        deps = [
            ":" + name + ".lib",
            rprefix + "//sandboxed_api:client",
        ],
        **common
    )

    native.cc_library(
        name = name + ".lib",
        deps = [lib],
        alwayslink = 1,  # All functions are linked into depending binaries
        **common
    )

    embed_name = ""
    embed_dir = ""
    if embed:
        embed_name = name

        sapi_cc_embed_data(
            srcs = [name + ".bin"],
            name = name + "_embed",
            namespace = namespace,
            **common
        )
        embed_dir = get_embed_dir()

    sapi_interface(
        name = name + ".interface",
        lib_name = lib_name,
        lib = lib,
        functions = functions,
        input_files = input_files,
        out = generated_header,
        embed_name = embed_name,
        embed_dir = embed_dir,
        namespace = namespace,
        isystem = ":" + name + ".isystem",
        **common
    )

    native.genrule(
        name = name + ".isystem",
        outs = [name + ".isystem.list"],
        cmd = """echo |
                 $(CC) -E -x c++ - -v 2>&1 |
                 awk '/> search starts here:/{flag=1;next}/End of search/{flag=0}flag' > $@
              """,
        toolchains = ["@bazel_tools//tools/cpp:current_cc_toolchain"],
    )
