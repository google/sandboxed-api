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
load("@rules_cc//cc:cc_test.bzl", "cc_test")
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

def _clang_generator_flags(cc_ctx, cpp_toolchain):
    flags = []

    # TODO(cblichmann): Get language standard from the toolchain
    flags.append("--extra-arg=-std=c++17")

    # Disable warnings in parsed code
    flags.append("--extra-arg=-Wno-everything")
    flags += ["--extra-arg=-D{}".format(d) for d in cc_ctx.defines.to_list()]
    flags += ["--extra-arg=-isystem{}".format(i) for i in cc_ctx.system_includes.to_list()]
    flags += ["--extra-arg=-iquote{}".format(i) for i in cc_ctx.quote_includes.to_list()]
    flags += ["--extra-arg=-I{}".format(d) for d in cc_ctx.includes.to_list()]
    return flags

def _lib_direct_headers(lib, cc_ctx):
    headers = []
    for h in cc_ctx.direct_headers:
        if h.extension != "h" or "/PROTECTED/" in h.path:
            continue

        # Include only headers coming from the target
        # not ones that it depends on by comparing the label packages.
        if (h.owner.package == lib.label.package):
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

    if ctx.attr.symbol_list_gen:
        args.append("--symbol_list_gen")

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

    if use_clang_generator:
        input_files += cpp_toolchain.all_files.to_list()
        extra_flags += _clang_generator_flags(cc_ctx, cpp_toolchain)
    else:
        append_all(extra_flags, "-D", cc_ctx.defines.to_list())
        append_all(extra_flags, "-isystem", cc_ctx.system_includes.to_list())
        append_all(extra_flags, "-iquote", cc_ctx.quote_includes.to_list())
        append_all(extra_flags, "-I", cc_ctx.includes.to_list())

    if ctx.attr.input_files:
        for f in ctx.files.input_files:
            input_files.append(f)
            input_files_paths.append(f.path)
    else:
        # Try to find files automatically
        input_files_paths += _lib_direct_headers(ctx.attr.lib, cc_ctx)

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
            default = 2,  # Note: always set by sapi_library
            values = [1, 2],
        ),
        "_generator_v1": make_exec_label(
            "//sandboxed_api/tools/python_generator:sapi_generator",
        ),
        "symbol_list_gen": attr.bool(default = False),
        "_generator_v2": make_exec_label(
            # TODO(cblichmann): Add prebuilt version of Clang based generator
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
        generator_at_head = True,
        generator_version = 2,
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
        header = "",
        input_files = [],
        deps = [],
        tags = [],
        generator_version = 1,
        visibility = None,
        compatible_with = None,
        default_copts = [],
        exec_properties = {}):
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
        (experimental). Version 1 uses the Python/libclang based `python_generator`,
        version 2 uses the newer C++ implementation that uses the full clang
        compiler front-end for parsing. Both emit equivalent Sandboxed APIs.
      visibility: Target visibility
      compatible_with: The list of environments this target can be built for,
        in addition to default-supported environments.
      default_copts: List of package level default copts, an additional
        attribute since copts already has default value.
      exec_properties: Dict of executable properties to be passed to the generated binary targets.
    """

    common = _common_kwargs(tags, visibility, compatible_with)
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
    cc_library(
        name = name,
        srcs = srcs,
        data = [":" + name + ".bin"] + data,
        hdrs = lib_hdrs,
        copts = default_copts + copts,
        defines = defines,
        deps = sort_deps(
            [
                "@abseil-cpp//absl/base:core_headers",
                "@abseil-cpp//absl/status",
                "@abseil-cpp//absl/status:statusor",
                "//sandboxed_api:sapi",
                "//sandboxed_api/util:status",
                "//sandboxed_api:vars",
            ] + deps +
            ([":" + name + "_embed"] if embed else []) +
            (default_deps if add_default_deps else []),
        ),
        **common
    )

    cc_binary(
        name = name + ".bin",
        linkopts = [
            "-ldl",  # For dlopen(), dlsym()
            # The sandboxing client must have access to all
            "-Wl,-E",  # symbols used in the sandboxed library, so these
        ] + exported_funcs,  # must be both referenced, and exported
        malloc = malloc,
        deps = [
            ":" + name + ".lib",
            "//sandboxed_api:client",
        ],
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
        out = generated_header + ".unformatted",
        embed_name = embed_name,
        embed_dir = embed_dir,
        namespace = namespace,
        api_version = api_version,
        generator_version = generator_version,
        limit_scan_depth = limit_scan_depth,
        **common
    )

    _clang_format_file(generated_header + ".unformatted", generated_header, **common)

def cc_sandboxed_library(
        name,
        lib,
        tags = [],
        visibility = None,
        compatible_with = None):
    """Creates a sandboxed drop-in replacement cc_library.

    NOTE: this functionality is experimental and may change in the future.

    The resulting library can be used instead of the original cc_library
    as dependency for other targets. The behavior is supposed to be identical
    to the original library, except that the library is sandboxed with a single
    global sandbox instance.

    Only limited set of types is supported in signatures of the library public
    functions. Any unsupported types will cause build failure.

    Any crashes or violations in the sandbox process crash the host process.

    Args:
      name: Name of the target
      lib: Label of the cc_library target to sandbox
      tags: Same as cc_library.tags
      visibility: Same as cc_library.visibility
      compatible_with: Same as cc_library.compatible_with
    """

    # Implementation outline:
    # 1. Run clang generator tool in sandboxed library mode.
    #    In this mode it takes all functions declared in the library public headers,
    #    and generates 3 files:
    #     - Sandboxee header file with wrappers for the public library functions.
    #       These wrappers have similar signature to the original functions,
    #       but also have some differences. For example, absl::string_view would be passed
    #       as a (const char*, size_t) pair of arguments so that SAPI supports it.
    #     - Sandboxee source file with implementation of the wrappers that call original functions.
    #       These wrappers will construct the string_view back from the pair of arguments.
    #     - Host source file with implementation of the public library functions.
    #       These implementations use SAPI sandbox to call the generated sandboxee wrappers
    #       in the sandbox, and will deconstruct string_view into the pair of arguments.
    # 2. Build cc_library with the sandboxee header and source files
    #    and dependency on the original library.
    # 3. Create a sapi_library for the sandboxee library created at step 2.
    #    This library also links in the generated host source file,
    #    so that it implements the original library interface verbatim.
    # 4. Create a transparent replacement rule that pretends to be a cc_library
    #    by assembling CcInfo from compilation context of the original library
    #    and linking context of the sapi_library created at step 3.
    #    Using the compilation context of the original library ensures that during
    #    compilation the replacement library "looks" exactly as the original library
    #    (this includes any use of defines/includes and any other cc_library attributes).

    # Unique prefix for things generated by sapi_library (e.g. FooSandbox class name).
    # TODO(dvyukov): add hash/flattening of the full library /path:name, just the name is not
    # necessarily globally unique.
    wrapper_name = "Sapi" + name
    common = _common_kwargs(tags, visibility, compatible_with)

    _sandboxed_library_gen(
        name = "_sandboxed_library_gen_" + name,
        lib = lib,
        lib_name = wrapper_name,
        sandboxee_hdr_out = name + ".sapi.sandboxee.h.unformatted",
        sandboxee_src_out = name + ".sapi.sandboxee.cc.unformatted",
        host_src_out = name + ".sapi.host.cc.unformatted",
        sapi_hdr = native.package_name() + "/_sapi_" + name + ".sapi.h",
        **common
    )

    _clang_format_file(name + ".sapi.sandboxee.h.unformatted", name + ".sapi.sandboxee.h", **common)
    _clang_format_file(name + ".sapi.sandboxee.cc.unformatted", name + ".sapi.sandboxee.cc", **common)
    _clang_format_file(name + ".sapi.host.cc.unformatted", name + ".sapi.host.cc", **common)

    cc_library(
        name = "_sapi_sandboxee_" + name,
        hdrs = [":" + name + ".sapi.sandboxee.h"],
        srcs = [":" + name + ".sapi.sandboxee.cc"],
        # Work-around broken global sapi_library mode (when functions are empty).
        # When functions are not empty, sapi_library adds -Wl,-u linker flags
        # that force linking of the sandboxee library. In global mode, it won't be linked.
        alwayslink = 1,
        deps = [
            lib,
            "//sandboxed_api:lenval_core",
        ],
        **common
    )

    sapi_library(
        name = "_sapi_" + name,
        lib = ":_sapi_sandboxee_" + name,
        lib_name = wrapper_name,
        srcs = [name + ".sapi.host.cc"],
        generator_version = 2,
        deps = [
            "//sandboxed_api:lenval_core",
            "@abseil-cpp//absl/log:check",
        ],
        **common
    )

    _sandboxed_library(
        name = name,
        lib = lib,
        sapi = ":_sapi_" + name,
        **common
    )

_sandboxed_library = rule(
    provides = [CcInfo],
    attrs = {
        "lib": attr.label(providers = [CcInfo]),
        "sapi": attr.label(providers = [CcInfo]),
    },
    implementation = lambda ctx: [CcInfo(
        compilation_context = ctx.attr.lib[CcInfo].compilation_context,
        linking_context = ctx.attr.sapi[CcInfo].linking_context,
    )],
)

def cc_sandboxed_library_test(
        name,
        lib,
        sandboxed_lib,
        deps = [],
        **kwargs):
    """Creates a pair of sandboxed/unsandboxed cc_test's for cc_sandboxed_library.

    NOTE: this functionality is experimental and may change in the future.

    This rule is supposed to be a replacement for any cc_test's for a library
    that is used with cc_sandboxed_library. It creates a pair of cc_test's
    that test both sandboxed and unsandboxed versions of the library.

    Args:
      name: Name of the target
      lib: Label of the normal unsandboxed cc_library target
      sandboxed_lib: Label of the cc_sandboxed_library target for the lib
      deps: Same as cc_library.deps, must not include lib/sandboxed_lib
      **kwargs: Passed to resulting cc_test's
    """

    cc_test(
        name = name + "_unsandboxed",
        deps = deps + [lib],
        **kwargs
    )

    cc_test(
        name = name + "_sandboxed",
        deps = deps + [sandboxed_lib],
        **kwargs
    )

    native.test_suite(
        name = name,
        tests = [
            ":" + name + "_unsandboxed",
            ":" + name + "_sandboxed",
        ],
    )

def _sandboxed_library_gen_impl(ctx):
    cpp_toolchain = find_cpp_toolchain(ctx)
    cc_ctx = ctx.attr.lib[CcInfo].compilation_context

    args = []
    args.append("--sandboxed_library_gen")
    args.append("--sapi_name={}".format(ctx.attr.lib_name))
    args.append("--sandboxee_hdr_out={}".format(ctx.outputs.sandboxee_hdr_out.path))
    args.append("--sandboxee_src_out={}".format(ctx.outputs.sandboxee_src_out.path))
    args.append("--host_src_out={}".format(ctx.outputs.host_src_out.path))
    args.append("--sapi_out={}".format(ctx.attr.sapi_hdr))
    args.append("--sapi_limit_scan_depth")
    args += _clang_generator_flags(cc_ctx, cpp_toolchain)
    args += _lib_direct_headers(ctx.attr.lib, cc_ctx)

    progress_msg = "Generating sandboxed library {}.".format(ctx.attr.lib_name)
    ctx.actions.run(
        inputs = cc_ctx.headers.to_list() + cpp_toolchain.all_files.to_list(),
        outputs = [ctx.outputs.sandboxee_hdr_out, ctx.outputs.sandboxee_src_out, ctx.outputs.host_src_out],
        arguments = args,
        mnemonic = "SandboxedLibraryGen",
        progress_message = progress_msg,
        executable = ctx.executable._generator,
    )

# Build rule that generates SAPI interface.
_sandboxed_library_gen = rule(
    implementation = _sandboxed_library_gen_impl,
    attrs = {
        "lib": attr.label(providers = [CcInfo]),
        "lib_name": attr.string(),
        "sandboxee_hdr_out": attr.output(),
        "sandboxee_src_out": attr.output(),
        "host_src_out": attr.output(),
        "sapi_hdr": attr.string(),
        "_generator": make_exec_label(
            "//sandboxed_api/tools/clang_generator:generator_tool",
        ),
    },
    toolchains = use_cpp_toolchain(),
)
