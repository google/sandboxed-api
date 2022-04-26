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

SYSTEM_LLVM_BAZEL_TEMPLATE = """package(default_visibility = ["//visibility:public"])
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
    ]),
    includes = ["llvm-project-include"],
    linkopts = [
        "-lncurses",
        "-lz",
        "-L%{llvm_lib_dir}",
        "-Wl,--start-group",
        %{llvm_libs}
        "-Wl,--end-group",
    ],
    visibility = ["@llvm-project//clang:__pkg__"],
)
# Fake support library
cc_library(name = "Support", deps = ["@llvm-project//llvm:llvm"])
"""

SYSTEM_CLANG_BAZEL = """package(default_visibility = ["//visibility:public"])
# Fake libraries that just depend on a big library with all files.
cc_library(name = "ast", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "basic", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "driver", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "format", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "frontend", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "lex", deps = ["@llvm-project//llvm:llvm"])
cc_library(name = "tooling", deps = ["@llvm-project//llvm:llvm"])
"""

def _use_system_llvm(ctx):
    found = False

    # Look for LLVM in known places
    llvm_dirs = ctx.execute(
        ["ls", "-1f"] +
        [
            "/usr/lib/llvm-{}/include/llvm/Support/InitLLVM.h".format(ver)
            for ver in [16, 15, 14, 13, 12, 11]  # Debian
        ] + [
            "/usr/include/llvm/Support/InitLLVM.h",  # Fedora and others
        ],
    ).stdout.splitlines()
    if llvm_dirs:
        llvm_dir = llvm_dirs[0].split("/include/llvm/")[0]
        for suffix in ["llvm", "llvm-c", "clang", "clang-c"]:
            ctx.symlink(
                llvm_dir + "/include/" + suffix,
                "llvm/llvm-project-include/" + suffix,
            )

        # Try to find the lib directory
        lib_dirs = ctx.execute(
            ["ls", "-d1f"] +
            [llvm_dir + "/lib64", llvm_dir + "/lib"],
        ).stdout.splitlines()
        if lib_dirs:
            found = True

    if found:
        # Create stub targets in sub-packages
        lib_dir = lib_dirs[0]  # buildifier: disable=uninitialized
        archives = ctx.execute(
            ["find", ".", "-maxdepth", "1"] +
            ["(", "-name", "libLLVM*.a", "-o", "-name", "libclang*.a", ")"],
            working_directory = lib_dir,
        ).stdout.splitlines()
        lib_strs = sorted(["\"-l{}\",".format(a[5:-2]) for a in archives])

        ctx.file(
            "llvm/BUILD.bazel",
            SYSTEM_LLVM_BAZEL_TEMPLATE
                .replace("%{llvm_lib_dir}", lib_dir)
                .replace("%{llvm_libs}", "\n".join(lib_strs)),
        )
        ctx.file("clang/BUILD.bazel", SYSTEM_CLANG_BAZEL)
    return found

def _overlay_directories(ctx, src_path, target_path):
    bazel_path = src_path.get_child("utils").get_child("bazel")
    overlay_path = bazel_path.get_child("llvm-project-overlay")
    script_path = bazel_path.get_child("overlay_directories.py")

    python_bin = ctx.which("python3")
    if not python_bin:
        python_bin = ctx.which("python")

    if not python_bin:
        fail("Failed to find python3 binary")

    cmd = [
        python_bin,
        script_path,
        "--src",
        src_path,
        "--overlay",
        overlay_path,
        "--target",
        target_path,
    ]
    exec_result = ctx.execute(cmd, timeout = 20)

    if exec_result.return_code != 0:
        fail(("Failed to execute overlay script: '{cmd}'\n" +
              "Exited with code {return_code}\n" +
              "stdout:\n{stdout}\n" +
              "stderr:\n{stderr}\n").format(
            cmd = " ".join([str(arg) for arg in cmd]),
            return_code = exec_result.return_code,
            stdout = exec_result.stdout,
            stderr = exec_result.stderr,
        ))

DEFAULT_LLVM_COMMIT = "2c494f094123562275ae688bd9e946ae2a0b4f8b"  # 2022-03-31
DEFAULT_LLVM_SHA256 = "59b9431ae22f0ea5f2ce880925c0242b32a9e4f1ae8147deb2bb0fc19b53fa0d"

def _llvm_configure_impl(ctx):
    commit = ctx.attr.commit
    sha256 = ctx.attr.sha256

    if ctx.attr.system_libraries:
        if _use_system_llvm(ctx):
            return
        if not commit:
            fail((
                "Failed to find LLVM and clang system libraries\n\n" +
                "Note: You may have to install llvm-13-dev and libclang-13-dev\n" +
                "      packages (or later versions) first.\n"
            ))

    if not commit:
        commit = DEFAULT_LLVM_COMMIT
        sha256 = DEFAULT_LLVM_SHA256

    ctx.download_and_extract(
        ["https://github.com/llvm/llvm-project/archive/{commit}.tar.gz".format(commit = commit)],
        "llvm-raw",
        sha256,
        "",
        "llvm-project-" + commit,
    )

    target_path = ctx.path("llvm-raw").dirname
    src_path = target_path.get_child("llvm-raw")
    _overlay_directories(ctx, src_path, target_path)

    # Create a starlark file with the requested LLVM targets
    ctx.file(
        "llvm/targets.bzl",
        "llvm_targets = " + str(ctx.attr.targets),
        executable = False,
    )

    # Set up C++ toolchain options. LLVM requires at least C++ 14.
    ctx.file(
        ".bazelrc",
        "build --cxxopt=-std=c++17 --host_cxxopt=-std=c++17",
        executable = False,
    )

DEFAULT_TARGETS = ["AArch64", "ARM", "PowerPC", "X86"]

llvm_configure = repository_rule(
    implementation = _llvm_configure_impl,
    local = True,
    configure = True,
    attrs = {
        "system_libraries": attr.bool(default = True),
        "commit": attr.string(),
        "sha256": attr.string(),
        "targets": attr.string_list(default = DEFAULT_TARGETS),
    },
)

def _llvm_zlib_disable_impl(ctx):
    ctx.file(
        "BUILD.bazel",
        """cc_library(name = "zlib", visibility = ["//visibility:public"])""",
        executable = False,
    )

llvm_zlib_disable = repository_rule(
    implementation = _llvm_zlib_disable_impl,
)

def _llvm_terminfo_disable(ctx):
    ctx.file(
        "BUILD.bazel",
        """cc_library(name = "terminfo", visibility = ["//visibility:public"])""",
        executable = False,
    )

llvm_terminfo_disable = repository_rule(
    implementation = _llvm_terminfo_disable,
)

def llvm_disable_optional_support_deps():
    maybe(llvm_zlib_disable, name = "llvm_zlib")
    maybe(llvm_terminfo_disable, name = "llvm_terminfo")
