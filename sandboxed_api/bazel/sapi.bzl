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

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load("//sandboxed_api/bazel:build_defs.bzl", "sapi_platform_copts")
load("//sandboxed_api/bazel:embed_data.bzl", "sapi_cc_embed_data")
load(
    "//sandboxed_api/bazel:proto.bzl",
    _sapi_proto_library = "sapi_proto_library",
)
load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain", "use_cpp_toolchain")

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

def _clang_generator_flags(ctx, cc_ctx, cpp_toolchain, input_files_paths):
    flags = []

    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cpp_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features + ["module_maps"],
    )
    compile_variables = cc_common.create_compile_variables(
        cc_toolchain = cpp_toolchain,
        feature_configuration = feature_configuration,
    )
    default_copts = cc_common.get_memory_inefficient_command_line(
        feature_configuration = feature_configuration,
        action_name = "c++-compile",
        variables = compile_variables,
    )

    # Use the defines that would be used by the toolchain normally for C++ compilation.
    # Add them first so that they are overridden by the user's copts.
    for copt in default_copts:
        if copt.startswith("-D") or copt.startswith("--target="):
            flags.append("--extra-arg=" + copt)

    # The GNU compiler is what is frequently used in practice, and it allows to
    # use more language extensions (e.g. _Bool type in C). However, compiling C
    # code with -std=gnu++ does not work, but -std=c++ does, and is required to
    # compile mixed C/C++ inputs, so this is the best we can do.
    std = "gnu++"
    for f in input_files_paths:
        if f.endswith(".c"):
            std = "c++"
            break

    # TODO(cblichmann): use the same language standard as the toolchain.
    # Note: it may be different for different files, but at least we could
    # infer the max standard version (year) used in the actual compilation.
    std += "20"

    flags.append("--extra-arg=-std=" + std)

    # Disable warnings in parsed code
    flags.append("--extra-arg=-Wno-everything")
    flags += ["--extra-arg=-D{}".format(d) for d in cc_ctx.defines.to_list()]
    flags += ["--extra-arg=-isystem{}".format(i) for i in cc_ctx.system_includes.to_list()]
    flags += ["--extra-arg=-iquote{}".format(i) for i in cc_ctx.quote_includes.to_list()]
    flags += ["--extra-arg=-I{}".format(d) for d in cc_ctx.includes.to_list()]
    return flags

def _lib_direct_headers(lib):
    headers = []
    for h in lib[CcInfo].compilation_context.direct_public_headers:
        if h.extension != "h" or "/PROTECTED/" in h.path:
            continue
        headers.append(h.path)

    return headers

def _clang_format_file(src, out, **kwargs):
    native.genrule(
        name = "_format_" + out,
        srcs = [":" + src],
        outs = [out],
        cmd = "cp $< $@",
        **kwargs
    )

def _sapi_interface_impl(ctx):
    cpp_toolchain = find_cpp_toolchain(ctx)

    # TODO(szwl): warn if input_files is not set and we didn't find anything
    input_files_paths = []
    input_files = []

    args = []
    outs = [ctx.outputs.out]
    append_arg(args, "--sapi_name", ctx.attr.lib_name)
    append_arg(args, "--sapi_out", ctx.outputs.out.path)
    append_arg(args, "--sapi_embed_dir", ctx.attr.embed_dir)
    append_arg(args, "--sapi_embed_name", ctx.attr.embed_name)
    append_arg(args, "--sapi_functions", ",".join(ctx.attr.functions))
    append_arg(args, "--sapi_ns", ctx.attr.namespace)
    append_arg(args, "--sapi_api_version", str(ctx.attr.api_version))
    append_arg(args, "--sapi_sandbox_mode", ctx.attr.sandbox_mode)

    if ctx.outputs.sandboxee_src_out:
        append_arg(args, "--sapi_sandboxee_src_out", ctx.outputs.sandboxee_src_out.path)
        outs.append(ctx.outputs.sandboxee_src_out)

    if ctx.attr.limit_scan_depth:
        args.append("--sapi_limit_scan_depth")

    if ctx.attr.symbol_list_gen:
        args.append("--symbol_list_gen")

    if ctx.attr.merged_sandboxee:
        args.append("--sapi_merged_sandboxee")

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

    if ctx.attr.input_files:
        for f in ctx.files.input_files:
            input_files.append(f)
            input_files_paths.append(f.path)
    else:
        # Try to find files automatically
        input_files_paths += _lib_direct_headers(ctx.attr.lib)

    input_files += cpp_toolchain.all_files.to_list()
    extra_flags += _clang_generator_flags(ctx, cc_ctx, cpp_toolchain, input_files_paths)

    args += extra_flags + input_files_paths

    progress_msg = "Generating {} from {} header files.".format(
        ctx.outputs.out.short_path,
        len(input_files_paths),
    )
    ctx.actions.run(
        inputs = input_files,
        outputs = outs,
        arguments = args,
        mnemonic = "SapiInterfaceGen",
        progress_message = progress_msg,
        executable = ctx.executable._generator,
    )

# Build rule that generates SAPI interface.
sapi_interface = rule(
    implementation = _sapi_interface_impl,
    fragments = ["cpp"],
    attrs = {
        "out": attr.output(mandatory = True),
        "sandboxee_src_out": attr.output(),
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
            default = 1,  # Always set by sapi_library
        ),
        "sandbox_mode": attr.string(default = "sandbox2"),
        "symbol_list_gen": attr.bool(default = False),
        "merged_sandboxee": attr.bool(default = False),
        # TODO(cblichmann): Add prebuilt version of Clang based generator
        "_generator": make_exec_label(
            "//sandboxed_api/tools/clang_generator:generator_tool",
        ),
    },
    toolchains = use_cpp_toolchain(),
)

def symbol_list_gen(name, lib, out, **kwargs):
    """Generates a list of symbols exported from the library.

    The generated file contains list of exported symbols, one per line.
    The file can be used with e.g. objcopy utility.

    NOTE: this functionality is experimental and may change in the future.

    Args:
      name: Name of the target
      lib: Label of the library target to sandbox
      out: Output file name with symbol list
      **kwargs: Additional arguments passed to sapi_interface rule
    """

    sapi_interface(
        name = name,
        lib_name = "unused",
        lib = lib,
        out = out,
        safe_wrapper_generation = False,
        symbol_list_gen = True,
        limit_scan_depth = True,
        **kwargs
    )

def _common_kwargs(tags, visibility, compatible_with):
    common = {
        "tags": tags,
    }
    if visibility:
        common["visibility"] = visibility
    if compatible_with != None:
        common["compatible_with"] = compatible_with
    return common

def sapi_library(
        name,
        lib,
        lib_name,
        malloc = "@bazel_tools//tools/cpp:malloc",
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
        input_files = [],
        deps = [],
        tags = [],
        visibility = None,
        compatible_with = None,
        default_copts = [],
        exec_properties = {},
        sandbox_mode = "sandbox2",
        strip_embedded_sandboxee = False):
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
      input_files: List of source files which the SAPI interface generator
        should scan for function declarations
      deps: Extra dependencies to add to the SAPI library
      tags: Extra tags to associate with the target
      visibility: Target visibility
      compatible_with: The list of environments this target can be built for,
        in addition to default-supported environments.
      default_copts: List of package level default copts, an additional
        attribute since copts already has default value.
      exec_properties: Dict of executable properties to be passed to the generated binary targets.
      sandbox_mode: Sandbox mode to use for the generated library. Either "sandbox2" (default) or "passthrough".
      strip_embedded_sandboxee: Whether to embed <name>.bin.stripped, without
        debug info, instead of <name>.bin. Only applies when embed is True.
    """

    common = _common_kwargs(tags, visibility, compatible_with)
    generated_file_prefix = name + ".sapi"
    generated_header = generated_file_prefix + ".h"
    generated_sandboxee_src = generated_file_prefix + ".sandboxee.cc"
    if sandbox_mode == "passthrough":
        embed = False
    in_process = sandbox_mode == "passthrough"

    sandboxee_linkopts = []

    lib_hdrs = hdrs or []
    lib_hdrs.append(generated_header)

    default_deps = ["//sandboxed_api/sandbox2"]

    sapi_data_deps = [":" + name + ".bin"] if not embed and not in_process else []

    backend_dep = []
    if sandbox_mode == "sandbox2":
        backend_dep = ["//sandboxed_api:sandbox2_backend"]
    elif sandbox_mode == "passthrough":
        backend_dep = ["//sandboxed_api:passthrough_backend", ":" + name + ".lib"]
    else:
        fail("Unsupported sandbox mode: " + sandbox_mode)

    cc_library(
        name = name + ".sandboxee",
        srcs = [generated_sandboxee_src],
        deps = [
            ":" + name + ".lib",
            "//sandboxed_api:client",
            "@abseil-cpp//absl/base:core_headers",
            "@abseil-cpp//absl/base:no_destructor",
            "@abseil-cpp//absl/container:flat_hash_map",
            "@abseil-cpp//absl/log",
            "@abseil-cpp//absl/strings:string_view",
            "//sandboxed_api:call",
            "//sandboxed_api:function_call_helper",
        ],
        alwayslink = 1,
        copts = default_copts,
        **common
    )
    client_message_handler = ":" + name + ".sandboxee"

    # Library that contains generated interface and sandboxed binary as a data
    # dependency. Add this as a dependency instead of original library.
    cc_library(
        name = name,
        srcs = srcs,
        data = sapi_data_deps + data,
        hdrs = lib_hdrs,
        copts = default_copts + copts,
        defines = defines,
        linkopts = sandboxee_linkopts if in_process else [],
        deps = sort_deps(
            [
                "@abseil-cpp//absl/base:core_headers",
                "@abseil-cpp//absl/base:no_destructor",
                "@abseil-cpp//absl/status:status_macros",
                "@abseil-cpp//absl/status:statusor",
                "@abseil-cpp//absl/status",
                "@abseil-cpp//absl/strings",
                "@abseil-cpp//absl/types:span",
                "//sandboxed_api:sapi",
                "//sandboxed_api/util:status",
                "//sandboxed_api:vars",
            ] + deps +
            ([":" + name + "_embed"] if embed else []) +
            (default_deps if add_default_deps else []) +
            ([client_message_handler] if in_process else []) +
            backend_dep,
        ),
        **common
    )

    if not in_process:
        cc_binary(
            name = name + ".bin",
            linkopts = sandboxee_linkopts,
            malloc = malloc,
            deps = [
                ":" + name + ".lib",
                "//sandboxed_api:client",
            ] + [client_message_handler],
            copts = default_copts,
            **common
        )

    cc_library(
        name = name + ".lib",
        deps = [lib],
        alwayslink = 1,  # All functions are linked into depending binaries
        copts = default_copts,
        **common
    )

    embed_name = ""
    embed_dir = ""
    if embed:
        embed_name = name

        sapi_cc_embed_data(
            name = name + "_embed",
            srcs = [":" + name + (".bin.stripped" if strip_embedded_sandboxee else ".bin")],
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
        out = generated_header + ".unformatted",
        sandboxee_src_out = generated_sandboxee_src + ".unformatted",
        embed_name = embed_name,
        embed_dir = embed_dir,
        namespace = namespace,
        api_version = api_version,
        limit_scan_depth = limit_scan_depth,
        sandbox_mode = sandbox_mode,
        **common
    )

    _clang_format_file(generated_header + ".unformatted", generated_header, **common)
    _clang_format_file(generated_sandboxee_src + ".unformatted", generated_sandboxee_src, **common)
