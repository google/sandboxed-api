# LodePng Sandboxed

Sandboxed version of the [lodepng](https://github.com/lvandeve/lodepng) library, using [Sandboxed API](https://github.com/google/sandboxed-api)

## Details

With Sandboxed API, many of the library's functions can be sandboxed. However, they need the `extern "C"` keyword defined so that name mangling does not happen, which is why a fork of the **lodepng** library is used. The only differences are found in the header file. An alternative to this is to define another library that wraps every needed function, specifying the required keyword.

Even if many of the functions from the library can be sandboxed, there are some that are not supported (those which have `std::vector` parameters, overloaded functions etc.). If you really need these functions, a solution is to implement a custom library that wraps around these functions in order to make them compatible.

## Examples

The code found in the **examples** folder features a basic use case of the library. An image is generated, encoded into a file and then decoded to check that the values are the same. The encoding part was based on [this example](https://github.com/lvandeve/lodepng/blob/master/examples/example_encode.c) while decoding was based on [this](https://github.com/lvandeve/lodepng/blob/master/examples/example_decode.c).

This folder is structured as:
- `main.cc` - unsandboxed example
- `sandbox.h` - custom sandbox policy
- `main_sandboxed.cc` - sandboxed version of the example
- `main_unit_test.cc` - testing file using [Google Test](https://github.com/google/googletest).

The executables generated from these files will create the png files in the current directory. However, for the 
`main_sandboxed.cc` file there is also the `images_path` flag which can be used to specify a different directory.

<!-- 
TODO

- ~~check return value of functions~~
- ~~integrate unit testing~~
- add more functions
- this readme
- include abseil flags for unit testing
- ~~improve tests (images, generating images etc.)~~
- clear redundant includes
- ~~check if security policy can be stricter~~
- ~~use addDirectoryAt instead of addDirectory~~
- ~~modify tests assertions~~
- ~~add useful prints to unit tests~~
- ~~move examples to examples folder~~
  
 -->
