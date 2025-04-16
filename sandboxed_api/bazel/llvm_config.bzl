# Copyright 2022 Google LLC
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

"""Repository rule that tries to find system provided LLVM packages."""

load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")

_SYSTEM_LLVM_BAZEL_TEMPLATE = """package(default_visibility = ["//visibility:public"])
# Create one hidden library with all LLVM headers that depends on all its
# static library archives. This will be used to provide individual library
# targets named the same as the upstream Bazel files.
cc_library(
    name = "llvm",
    hdrs = glob([
        "llvm-project-include/clang-c/**/*.h",
        "llvm-project-include/clang/**/*.def",
        "llvm-project-include/clang/**/*.h",
        "llvm-project-include/clang/**/*.inc",
        "llvm-project-include/llvm-c/**/*.h",
        "llvm-project-include/llvm/**/*.def",
        "llvm-project-include/llvm/**/*.h",
        "llvm-project-include/llvm/**/*.inc",
    ], allow_empty = True),
    includes = ["llvm-project-include"],
    linkopts = [
        "-lncurses",
        {llvm_system_libs}
        {llvm_lib_dir}
        "-Wl,--start-group",
        {llvm_libs}
        "-Wl,--end-group",
    ],
    visibility = ["@llvm-project//clang:__pkg__"],
)
# Fake llvm libraries
cc_library(name = "Support", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "config", deps = ["@llvm-project//llvm:llvm"])
"""

_SYSTEM_CLANG_BAZEL = """package(default_visibility = ["//visibility:public"])
# Fake libraries that just depend on a big library with all files.
cc_library(name = "ast", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "basic", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "driver", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "format", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "frontend", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "lex", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "serialization", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "tooling", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "tooling_core", deps = ["@llvm-project//llvm:llvm"])
"""

def _locate_llvm_config_tool(repository_ctx):
    """Searches for the llvm-config tool on the system.

    It will try to find llvm-config starting with `version` (which can be configured) and going down
    to 10 and lastly trying to find llvm-config (without version number). This assures that we find
    the latest version of llvm-config.

    Returns:
        The path to the llvm-config tool.
    """
    max_version = 20
    min_version = 18

    llvm_config_tool = repository_ctx.execute(
        ["which"] +  # Prints all arguments it finds in the system PATH
        ["llvm-config-{}".format(ver) for ver in range(max_version, min_version, -1)] +
        ["llvm-config"],
    )
    if not llvm_config_tool.stdout:
        fail("Local llvm-config lookup failed")
    return llvm_config_tool.stdout.splitlines()[0]

def _get_llvm_config_output(repository_ctx, llvm_config_tool):
    """Runs llvm-config and returns the output.

    Returns:
        A dict with the following keys:
            include_dir: The path to the include directory.
            system_libs: The list of system libraries.
            lib_dir: The path to the library directory.

    Args:
        repository_ctx: The context.
        llvm_config_tool: The path to the llvm-config tool.
    """

    llvm_config = repository_ctx.execute([
        llvm_config_tool,
        "--link-static",
        "--includedir",  # Output line 0
        "--libdir",  # Output line 1
        "--libs",  # Output line 2
        "--system-libs",  # Output line 3
        "engine",
        "option",
    ])
    if llvm_config.return_code != 0:
        fail("llvm-config failed: {}".format(llvm_config.stderr))
    output = llvm_config.stdout.splitlines()

    return {
        "include_dir": output[0],
        "system_libs": output[3].split(" "),
        "lib_dir": output[1].split(" ")[0],
    }

def _create_llvm_build_files(repository_ctx, llvm_config):
    """Creates the BUILD.bazel files for LLVM and Clang.

    Args:
        repository_ctx: The context.
        llvm_config: The output dict of _get_llvm_config_output.
    """

    include_dir = llvm_config["include_dir"]
    for suffix in ["llvm", "llvm-c", "clang", "clang-c"]:
        repository_ctx.symlink(
            include_dir + "/" + suffix,
            "llvm/llvm-project-include/" + suffix,
        )

    system_libs = llvm_config["system_libs"]
    lib_dir = llvm_config["lib_dir"]

    # Sadly there's no easy way to get to the Clang library archives
    archives = repository_ctx.execute(
        ["find", ".", "-maxdepth", "1"] +
        ["(", "-name", "libLLVM*.a", "-o", "-name", "libclang*.a", ")"],
        working_directory = lib_dir,
    ).stdout.splitlines()
    lib_strs = sorted(['"-l{}",'.format(a[5:-2]) for a in archives])

    paddeed_newline = "\n" + " " * 8
    repository_ctx.file(
        "llvm/BUILD.bazel",
        _SYSTEM_LLVM_BAZEL_TEMPLATE.format(
            llvm_system_libs = paddeed_newline.join(['"{}",'.format(s) for s in system_libs]),
            llvm_lib_dir = '"-L{}",'.format(lib_dir),
            llvm_libs = paddeed_newline.join(lib_strs),
        ),
    )

def _create_clang_build_files(repository_ctx):
    """Creates the BUILD.bazel files for Clang."""
    repository_ctx.file("clang/BUILD.bazel", _SYSTEM_CLANG_BAZEL)

def _verify_llvm_dev_headers_are_installed(repository_ctx, llvm_config_tool):
    """Verifies that the LLVM dev headers are installed."""

    llvm_major_version = repository_ctx.execute([
        llvm_config_tool,
        "--version",
    ])
    if llvm_major_version.return_code != 0:
        fail("llvm-config --version failed:\n{}\n".format(llvm_major_version.stderr))

    major_version = llvm_major_version.stdout.split(".")[0]
    for lib in ["llvm", "clang"]:
        llvm_dev_headers = repository_ctx.execute(
            ["stat"] +
            ["/usr/lib/llvm-{}/include/{}".format(major_version, lib)],
        )
        if llvm_dev_headers.return_code != 0:
            fail("Locating {} headers failed. You may have to install libclang-{}-dev\n{}\n".format(
                lib,
                major_version,
                llvm_dev_headers.stderr,
            ))

def _use_system_llvm(repository_ctx):
    """Looks for local LLVM and then prepares BUILD files.

    Returns:
        True if LLVM was found, or otherwise Fails.
    """
    llvm_config_tool = _locate_llvm_config_tool(repository_ctx)
    llvm_config = _get_llvm_config_output(repository_ctx, llvm_config_tool)
    _verify_llvm_dev_headers_are_installed(repository_ctx, llvm_config_tool)
    _create_llvm_build_files(repository_ctx, llvm_config)
    _create_clang_build_files(repository_ctx)
    return True

def _llvm_configure_impl(ctx):
    """Implementation of the `llvm_configure` rule."""

    if _use_system_llvm(ctx):
        return
    fail((
        "Failed to find LLVM and clang system libraries\n\n" +
        "Note: You may have to install llvm-13-dev and libclang-13-dev\n" +
        "      packages (or later versions) first.\n"
    ))

def _llvm_zlib_disable_impl(ctx):
    ctx.file(
        "BUILD.bazel",
        """cc_library(name = "zlib", visibility = ["//visibility:public"])""",
        executable = False,
    )

def _llvm_terminfo_disable_impl(ctx):
    ctx.file(
        "BUILD.bazel",
        """cc_library(name = "terminfo", visibility = ["//visibility:public"])""",
        executable = False,
    )

# We use this `module_extension` directly in MODULE.bazel, configure it with the values and
# then use `use_repo` to add it to the workspace.
llvm = module_extension(
    tag_classes = {
        "disable_llvm_zlib": tag_class(),
        "disable_llvm_terminfo": tag_class(),
    },
    implementation = lambda ctx: _llvm_module_implementation(ctx),
)

def _llvm_module_implementation(module_ctx):
    """Implementation of the `llvm_configure` module_extension."""
    if len(module_ctx.modules) != 1:
        fail("llvm_configure module_extension must be used with exactly one module")

    llvm_configure(
        name = "llvm-project",
    )

    for _ in module_ctx.modules[0].tags.disable_llvm_zlib:
        maybe(llvm_zlib_disable, name = "llvm_zlib")
    for _ in module_ctx.modules[0].tags.disable_llvm_terminfo:
        maybe(llvm_terminfo_disable, name = "llvm_terminfo")

# DON'T USE THIS RULE DIRECTLY.
llvm_configure = repository_rule(
    implementation = _llvm_configure_impl,
    local = True,
    configure = True,
)

# DO NOT USE THIS RULE DIRECTLY.
llvm_zlib_disable = repository_rule(
    implementation = _llvm_zlib_disable_impl,
    local = True,
)

# DO NOT USE THIS RULE DIRECTLY.
llvm_terminfo_disable = repository_rule(
    implementation = _llvm_terminfo_disable_impl,
    local = True,
)
