# LibUV Sandbox

This library is a sandboxed version of [LibUV](https://libuv.org/), implemented
using Sandboxed API.

## Setup

The repository can be cloned using:
```
git clone --recursive [URL to this repo]
```
The `--recursive` flag ensures that submodules are also cloned.

Alternatively, if the repository has already been cloned but the submodules have
not, these can be cloned using:
```
git submodule update --init --recursive
```

The full list of Sandboxed API dependencies can be found on
[Sandboxed API Getting Started page](https://developers.google.com/code-sandboxing/sandboxed-api/getting-started).

The following commands, used from the current `libuv/` directory, build the
library:
```
mkdir -p build
cd build
cmake .. -G Ninja -D SAPI_ROOT=[path to sandboxed-api]
cmake --build .
```

## Implementation details

LibUV can't be directly sandboxed by Sandboxed API. Because of the size and
complexity of the library, doing so creates some compilation errors and does not
create the correct APIs for all methods. The solution for this issue is creating
a wrapper library, whose methods (which can be sandboxed properly) internally
call the original LibUV's methods.

Using these wrapper methods is extremely simple, the only relevant difference is
that they have a `sapi_` prefix before their names (e.g. `uv_loop_init` becomes
`sapi_uv_loop_init`). The only exception is the variadic method
`uv_loop_configure`. This has two wrappers, `sapi_uv_loop_configure` (with no
additional parameters) and `sapi_uv_loop_configure_int` (with an additional
`int` parameter). Currently, these are the only valid calls to the method in
LibUV.

#### Wrapper generator

The wrapper is generated automatically by a Python script that is called by
CMake at build time. This script can be found in the `generator` folder.

#### Function pointers

The functions whose pointers will be passed to the library's methods
(*callbacks*) can't be implemented in the files making use of the library, but
must be in other files. These files must be compiled together with the library,
and this is done by adding their absolute path to the CMake variable
`SAPI_UV_CALLBACKS`.

The pointers can then be obtained using an `RPCChannel` object, as shown in the
example `idle-basic.cc`.

## Examples

The `examples` directory contains the sandboxed versions of example source codes
taken from from [LibUV's User Guide](https://docs.libuv.org/en/v1.x/guide.html).
More information about each example can be found in the examples'
[README](examples/README.md).

To build these examples when building the library, the CMake variable
`SAPI_UV_ENABLE_EXAMPLES` must be set to `ON`. This enables Sandboxed API
examples as well.

## Testing

The `tests` folder contains some test cases created using Google Test.

To build these tests when building the library, the CMake variable
`SAPI_UV_ENABLE_TESTS` must be set to `ON`. This enables Sandboxed API tests as
well.

## Policies

Each example and test has an ad-hoc policy implemented on its own file. These
policies can be used as references for pieces of code that perform similar
operations. For example, the `helloworld.cc` example has a policy that only
allows the strictly necessary syscalls for running a loop.

## Callbacks

The `callbacks.h` and `callbacks.cc` files in the `callbacks` folder implement
all the callbacks used by examples and tests.
