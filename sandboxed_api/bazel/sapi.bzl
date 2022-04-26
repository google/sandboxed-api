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

"""Starlark rules for projects using Sandboxed API."""

load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("//sandboxed_api/bazel:embed_data.bzl", "sapi_cc_embed_data")
load(
    "//sandboxed_api/bazel:proto.bzl",
    _sapi_proto_library = "sapi_proto_library",
)
load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain")

# Reexport symbols
sapi_proto_library = _sapi_proto_library

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

def make_exec_label(label):
    return attr.label(
        default = label,
        cfg = "exec",
        allow_files = True,
        executable = True,
    )

# buildifier: disable=function-docstring
def select_generator(ctx):
    if ctx.attr.generator_version == 1:
        return ctx.executable._generator_v1
    return ctx.executable._generator_v2

def sort_deps(deps):
    """Sorts a list of dependencies.

    This does not convert absolute references targeting the current package
    into relative ones.

    Args:
      deps: List of labels to be sorted
    Returns:
      A sorted list of dependencies, with local deps (starting with ":") first.
    """

    deps = depset(deps).to_list()
    colon_deps = [x for x in deps if x.startswith(":")]
    other_deps = [x for x in deps if not x.startswith(":")]
    return sorted(colon_deps) + sorted(other_deps)

def _sapi_interface_impl(ctx):
    cpp_toolchain = find_cpp_toolchain(ctx)
    generator = select_generator(ctx)
    use_clang_generator = ctx.attr.generator_version == 2

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

    if ctx.attr.limit_scan_depth:
        args.append("--sapi_limit_scan_depth")

    # Parse provided files.

    # The parser doesn't need the entire set of transitive headers
    # here, just the top-level cc_library headers.
    #
    # Allow all headers through that contain the dependency's
    # package path. Including extra headers is harmless except that
    # we may hit Bazel's file-count limit, so be conservative and
    # pass a lot through that we don't strictly need.
    #
    extra_flags = []
    cc_ctx = ctx.attr.lib[CcInfo].compilation_context

    # Append all headers as dependencies
    input_files += cc_ctx.headers.to_list()

    quote_includes = cc_ctx.quote_includes.to_list()

    if use_clang_generator:
        input_files += cpp_toolchain.all_files.to_list()

        # TODO(cblichmann): Get language standard from the toolchain
        extra_flags.append("--extra-arg=-std=c++17")

        # Disable warnings in parsed code
        extra_flags.append("--extra-arg=-Wno-everything")
        extra_flags += ["--extra-arg=-isystem{}".format(d) for d in cpp_toolchain.built_in_include_directories]
        extra_flags += ["--extra-arg=-D{}".format(d) for d in cc_ctx.defines.to_list()]
        extra_flags += ["--extra-arg=-isystem{}".format(i) for i in cc_ctx.system_includes.to_list()]
        extra_flags += ["--extra-arg=-iquote{}".format(i) for i in quote_includes]
    else:
        append_all(extra_flags, "-D", cc_ctx.defines.to_list())
        append_all(extra_flags, "-isystem", cc_ctx.system_includes.to_list())
        append_all(extra_flags, "-iquote", quote_includes)

    if ctx.attr.input_files:
        for f in ctx.files.input_files:
            input_files.append(f)
            input_files_paths.append(f.path)
    else:
        # Try to find files automatically
        for h in cc_ctx.direct_headers:
            if h.extension != "h" or "/PROTECTED/" in h.path:
                continue

            # Include only headers coming from the target
            # not ones that it depends on by comparing the label packages.
            if (h.owner.package == ctx.attr.lib.label.package):
                input_files_paths.append(h.path)

    if use_clang_generator:
        args += extra_flags + input_files_paths
    else:
        append_arg(args, "--sapi_in", ",".join(input_files_paths))
        args += ["--"] + extra_flags

    progress_msg = ("Generating {} from {} header files." +
                    "").format(ctx.outputs.out.short_path, len(input_files_paths))
    ctx.actions.run(
        inputs = input_files,
        outputs = [ctx.outputs.out],
        arguments = args,
        mnemonic = "SapiInterfaceGen",
        progress_message = progress_msg,
        executable = generator,
    )

# Build rule that generates SAPI interface.
sapi_interface = rule(
    implementation = _sapi_interface_impl,
    attrs = {
        "out": attr.output(mandatory = True),
        "embed_dir": attr.string(),
        "embed_name": attr.string(),
        "functions": attr.string_list(
            allow_empty = True,
            default = [],
        ),
        "input_files": attr.label_list(
            providers = [CcInfo],
            allow_files = True,
        ),
        "lib": attr.label(
            providers = [CcInfo],
            mandatory = True,
        ),
        "lib_name": attr.string(mandatory = True),
        "namespace": attr.string(),
        "limit_scan_depth": attr.bool(default = False),
        "api_version": attr.int(
            default = 1,
            values = [1],  # Only a single version is defined right now
        ),
        "generator_version": attr.int(
            default = 1,
            values = [1, 2],
        ),
        "_generator_v1": make_exec_label(
            "//sandboxed_api/tools/generator2:sapi_generator",
        ),
        "_generator_v2": make_exec_label(
            # TODO(cblichmann): Add prebuilt version of Clang based generator
            "//sandboxed_api/tools/clang_generator:generator_tool",
        ),
        "_cc_toolchain": attr.label(
            default = Label("@bazel_tools//tools/cpp:current_cc_toolchain"),
        ),
    },
    output_to_genfiles = True,
    toolchains = ["@bazel_tools//tools/cpp:toolchain_type"],
)

def sapi_library(
        name,
        lib,
        lib_name,
        namespace = "",
        api_version = 1,
        embed = True,
        add_default_deps = True,
        limit_scan_depth = False,
        srcs = [],
        data = [],
        hdrs = [],
        copts = sapi_platform_copts(),
        defines = [],
        functions = [],
        header = "",
        input_files = [],
        deps = [],
        tags = [],
        generator_version = 1,
        visibility = None):
    """Provides the implementation of a Sandboxed API library.

    Args:
      name: Name of the sandboxed library
      lib: Label of the library target to sandbox
      lib_name: Name of the class which will proxy the library functions from
        the functions list
      malloc: Override the default dependency on malloc
      namespace: A C++ namespace identifier to place the API class into
      embed: Whether the SAPI library should be embedded inside the host code
      add_default_deps: Add SAPI dependencies to target (deprecated)
      limit_scan_depth: Limit include depth for header generator (deprecated)
      api_version: Which version of the Sandboxed API to generate. Currently,
        only version 1 is defined.
      srcs: Any additional sources to include with the sandboxed library
      data: To be used with srcs, any additional data files to make available
        to the sandboxed library.
      hdrs: Like srcs, any additional headers to include with the sandboxed
        library
      copts: Add these options to the C++ compilation command. See
        cc_library.copts.
      defines: List of defines to add to the compile line. See
        cc_library.defines.
      functions: A list for function to use from host code
      header: If set, do not generate a header, but use the specified one
        (deprecated).
      input_files: List of source files which the SAPI interface generator
        should scan for function declarations
      deps: Extra dependencies to add to the SAPI library
      tags: Extra tags to associate with the target
      generator_version: Which version the the interface generator to use
        (experimental). Version 1 uses the Python/libclang based `generator2`,
        version 2 uses the newer C++ implementation that uses the full clang
        compiler front-end for parsing. Both emit equivalent Sandboxed APIs.
      visibility: Target visibility
    """

    common = {
        "tags": tags,
    }
    if visibility:
        common["visibility"] = visibility

    generated_header = name + ".sapi.h"

    # Reference (pull into the archive) required functions only. If the functions'
    # array is empty, pull in the whole archive (may not compile with MSAN).
    exported_funcs = ["-Wl,-u," + s for s in functions]
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

    default_deps = ["//sandboxed_api/sandbox2"]

    # Library that contains generated interface and sandboxed binary as a data
    # dependency. Add this as a dependency instead of original library.
    native.cc_library(
        name = name,
        srcs = srcs,
        data = [":" + name + ".bin"] + data,
        hdrs = lib_hdrs,
        copts = copts,
        defines = defines,
        deps = sort_deps(
            [
                "@com_google_absl//absl/base:core_headers",
                "@com_google_absl//absl/status",
                "@com_google_absl//absl/status:statusor",
                "//sandboxed_api:sapi",
                "//sandboxed_api/util:status",
                "//sandboxed_api:vars",
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
            # The sandboxing client must have access to all
            "-Wl,-E",  # symbols used in the sandboxed library, so these
        ] + exported_funcs,  # must be both referenced, and exported
        deps = [
            ":" + name + ".lib",
            "//sandboxed_api:client",
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
            name = name + "_embed",
            srcs = [name + ".bin"],
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
        api_version = api_version,
        generator_version = generator_version,
        limit_scan_depth = limit_scan_depth,
        **common
    )
