# Library


## BUILD.bazel

Here, you'll prepare a build target, that your [host code](host-code.md)
will make use of.

Start by preparing a [sapi_library()][sapi_library] target in your `BUILD.bazel`
file.

For reference, you can take a peek at a working example from the
[zlib example](../examples/zlib/lib/BUILD.bazel).

```python
load(
    "//sandboxed_api/tools/generator:sapi_generator.bzl",
    "sapi_library",
)

sapi_library(
    name = "zlib-sapi",
    srcs = [],   # Extra code compiled with the SAPI library
    hdrs = []    # Leave empty if embedded SAPI libraries are used, and the
                 # default sandbox policy is sufficient.
    embed = True,
    functions = [
        "deflateInit_",
        "deflate",
        "deflateEnd",
    ],
    lib = "@zlib//:zlibonly",
    lib_name = "Zlib",
    namespace = "sapi::zlib",
)
```

* **`name`** - name for your SAPI target
* **`srcs`** - any additional sources that you'd like to include with your
  Sandboxed API library - typically, it's not necessary, unless you want to
  provide your SAPI Library sandbox definition in a .cc file, and not in the
  `sandbox.h` file.
* **`hdrs`** - as with **`srcs`**. Typically your sandbox definition (sandbox.h)
  should go here, or empty, if embedded SAPI library is used, and the default
    sandbox policy is sufficient.
* **`functions`** - a list of functions that you'd like to use in your host
  code. Leaving this list empty will try to export and wrap all functions found
  in the library.
* **`embed`** - whether the SAPI library should be embedded inside host code,
  so the SAPI Sandbox can be initialized with the
  `::sapi::Sandbox::Sandbox(FileToc*)` constructor.
* **`lib`** - (mandatory) the library target you want to sandbox and expose to
    the host code.
* **`lib_name`** - (mandatory) name of the object which is proxying your library
  functions from the `functions` list. You will call functions from the
  sandboxed library via this object.
* **`input_files`** - list of source files, which SAPI interface generator
  should scan for library's function declarations. Library's exported headers
  are always scanned, so `input_files` can usually be left empty.
* **`namespace`** - a C++ namespace identifier to place API object defined in
  `lib_name` into. Defaults to `sapigen`.
* **`deps**`** - a list of any additional dependency targets to add. Typically
  not necessary.
* **`header`** - name of the header file to use instead of the generated one.
  Do not use if you want to auto-generate the code.


## `sapi_library()` Rule Targets

For the above definition, `sapi_library()` build rule provides the following
targets:

* **`zlib-sapi`** - sandboxed library, substitiution for normal cc_library;
    consists of **`zlib_sapi.bin`** and sandbox dependencies
* **`zlib-sapi.interface`** - generated library interface
* **`zlib-sapi.embed`** - `cc_embed_data()` target used to embed sandboxee in
  the binary. See `bazel/embed_data.bzl`.
* **`zlib-sapi.bin`** - sandboxee binary, consists of small communication stub
  and the library that is being sandboxed.


## Interface Generation

__`zlib-sapi`__ target creates target library with small communication stub
wrapped in [Sandbox2](../sandbox2/README.md). To be able to use the stub and
code within the sanbox, you should generate the interface file.

There are two options:

1.  Add dependency on __`zlib-sapi.interface`__. This will auto-generate a
    header that you can include in your code - the header name is of the form:
    __`TARGET_NAME`__`.sapi.h`.
2.  Run `bazel build TARGET_NAME.interface`, save generated header in your
    project and include it in the code. You will also need to add the `header`
    argument to the `sapi_library()` rule to indicate that you will skip code
    generation.


## Sandbox Description (`sandbox.h`)

**Note**: If the default SAPI Sandbox policy is sufficient, and the constructor
used is **`::sapi::Sandbox::Sandbox(FileToc*)`**, then this file might not be
necessary.

In this step you will prepare the sandbox definition file (typically named
`sandbox.h`) for your library.

The goal of this is to tell the SAPI code where the sandboxed library can be
found, and how it should be contained.

At first, you should tell the SAPI code what your sandboxed library should be
allowed to do in terms of security policies and other process constraints. In
order to do that, you will have to implement and instantiate an object based on
the [::sapi::Sandbox](../sandbox.h) class.

This object will also specify where your SAPI Library can be found
and how it should be executed (though you can depend on default settings).

A working example of such SAPI object definition file can be found
[here](../examples/zlib/lib/sandbox.h).

In order to familiarize yourself with the Sandbox2 policies, you might want to
take a look at the [Sandbox2 documenation](../sandbox2/README.md).
