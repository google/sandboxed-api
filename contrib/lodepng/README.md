# LodePNG Sandboxed API

This library was sandboxed as part of Google's summer 2020 internship program
([blog post](https://security.googleblog.com/2020/12/improving-open-source-security-during.html)).

This directory contains a sandbox for the
[LodePNG](https://github.com/lvandeve/lodepng) library.

## Details

With Sandboxed API, many of the library's functions can be sandboxed. However, they need the `extern "C"` keyword defined so that name mangling does not happen, which is why a patch that adds it is used. The only differences are found in the header file. An alternative to this is to define another library that wraps every needed function, specifying the required keyword.

Even if many of the functions from the library can be sandboxed, there are some that are not supported (those which have `std::vector` parameters, overloaded functions etc.). If you really need these functions, a solution is to implement a custom library that wraps around these functions in order to make them compatible.

## Patches

In the **patches** folder there is a patch file that adds `extern "C"` to the required functions in the header file in order to sandbox them. This patch is applied automatically during the build phase.

## Build

Run the following commands:

`mkdir -p build && cd build`

`cmake .. -G Ninja`

`cmake --build .`


The example binary files can be found in `build/examples`.

## Examples

The code found in the **examples** folder features a basic use case of the library. An image is generated, encoded into a file and then decoded to check that the values are the same. The encoding part was based on [this example](https://github.com/lvandeve/lodepng/blob/master/examples/example_encode.c) while decoding was based on [this](https://github.com/lvandeve/lodepng/blob/master/examples/example_decode.c).

This example code is structured as:
- `main_unsandboxed.cc` - unsandboxed example
- `main_sandboxed.cc` - sandboxed version of the example
- `main_unit_test.cc` - tests(using [Google Test](https://github.com/google/googletest)).

On top of those files, there are other files used by all three of the examples:
- `sandbox.h` - custom sandbox policy
- `helpers.h` and `helpers.cc` - constants and functions used in the main files.

The executables generated from these files will create a temporary directory in the current working path. Inside that directory the two generated **png** files will be created. At the end, the directory is deleted. If those programs do not stop midway or return a failure code, then everything works fine.
