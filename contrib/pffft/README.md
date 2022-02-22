# Sandboxing PFFFT library

This library was sandboxed as part of Google's summer 2020 internship program
([blog post](https://security.googleblog.com/2020/12/improving-open-source-security-during.html)).

Build System: CMake
OS: Linux

### How to use from an existing Project

If your project does not include Sandboxed API as a dependency yet, add the
following lines to the main `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(sandboxed-api
  GIT_REPOSITORY https://github.com/google/sandboxed-api
  GIT_TAG        main  # Or pin a specific commit/tag
)
FetchContent_MakeAvailable(sandboxed-api)  # CMake 3.14 or higher

add_sapi_subdirectory(contrib/pffft)
```

The `add_sapi_subdirectory()` macro sets up the source and binary directories
for the sandboxed jsonnet targets.

Afterwards your project's code can link to `sapi_contrib::pffft` and use the
generated header `pffft_sapi.sapi.h`. An example sandbox policy can be found
in `main_pffft_sandboxed.cc`.

### For testing:
`cd build`, then `./pffft_sandboxed`

### For debug:
display custom info with
`./pffft_sandboxed --logtostderr`

## ***About the project***

PFFFT library is concerned with 1D Fast-Fourier Transformations finding a
compromise between accuracy and speed. It deals with real and complex
vectors, both cases being illustrated in the testing part (`test_pffft.c`
for initially and original version, `main_pffft_sandboxed.cc` for our
currently implemented sandboxed version).
The original files can be found at: https://bitbucket.org/jpommier/pffft/src.*

The purpose of sandboxing is to limit the permissions and capabilities of
library’s methods, in order to secure the usage of them.
After obtaining the sandbox, the functions will be called through an
Sandbox API (being called `api` in the current test) and so, the
operations, system calls or namspaces access may be controlled.
From both `pffft.h` and `fftpack.h` headers, useful methods are added to
sapi library builded with CMake. There is also a need to link math library
as the transformations made require mathematical operators.
Regarding the testing of the methods, one main is doing this job by
iterating through a set of values, that represents the accuracy of
transformations and print the speed for each value and type of
transformation. More specifically, the input length is the target for
accuracy (named as `n`) and it stands for the number of data points from
the series that calculate the result of transformation. It is also
important to mention that the `complex` variable stands for a boolean value
that tells the type of transformation (0 for REAL and 1 for COMPLEX) and
it is taken into account while testing.
In the end, the performance of PFFFT library it is outlined by the output.
There are two output formats available, from which you can choose through
`--output_format=` command-line flag.
Without using this type of argument when running, the output format is set
by default.*

#### CMake observations resume:

* linking pffft and fftpack (which contains necessary functions for pffft)
* set math library

#### Sandboxed main observations resume:

* containing two testing parts (fft / pffft benchmarks)
* showing the performance of the transformations implies
  testing them through various FFT dimenstions.
  Variable n, the input length, will take specific values
  meaning the number of points to which it is set the calculus
  (more details of mathematical purpose of n - https://en.wikipedia.org/wiki/Cooley%E2%80%93Tukey_FFT_algorithm).
* output shows speed depending on the input length
* use `--output_format=0` or `--output_format=1` arguments to choose between output formats.
  `0` is for a detailed output, while `1` is only displaying each transformation process speed.

### Bugs history
1. [Solved] pffft benchmark bug: "Sandbox not active"

   n = 64, status OK, `pffft_transform` generates error
   n > 64, status not OK
   Problem on initialising `absl::StatusOr<PFFFT_Setup *> s;` the memory that stays
   for s is not the same with the address passed in `pffft_transform` function.
   (`sapi::v::GenericPtr` - to be changed)

   Temporary solution: change the generated files to accept
   `uintptr_t` instead of `PFFFT_Setup`

   Solution: using `sapi::v::RemotePtr` instead of `sapi::v::GenericPtr`
   to access the memory of object `s`

2. [Unresolved] compiling bug: "No space left on device"

   The building process creates some `embed` files that use lots of
   memory, trying to write them on `/tmp`.

   Temporary solution: clean /tmp directory by `sudo rm -rf /tmp/*`
