# Examples

We have prepared some examples, which might help you to implement your first
Sandboxed API library.


## Sum

A demo library implementing a few [C functions](../examples/sum/lib/sum.c) and a
single [C++ function](../examples/sum/lib/sum_cpp.cc).
It uses ProtoBuffs to exchange data between host code and the SAPI Library.

* The sandbox definition can be found in the
  [sandbox.h](../examples/sum/lib/sandbox.h) file.
* The (automatically generated) function annotation file (a file providing
  prototypes of sandboxed functions) can be found in
  `bazel-out/genfiles/sandboxed_api/examples/sum/lib/sum-sapi.sapi.h`
  after a Bazel build.
* The actual execution logic (a.k.a. host code) making use of the exported
  sandboxed procedures can be found in [main_sum.cc](../examples/sum/main_sum.cc).


## zlib

This is a demo implementation (functional, but currently not used in production)
for the zlib library exporting some of its functions, and making them available
to the [host code](../examples/zlib/main_zlib.cc).

The demonstrated functionality of the host code is decoding of zlib streams
from stdin to stdout.

This SAPI library doesn't use the `sandbox.h` file, as it uses the default
Sandbox2 policy, and an embedded SAPI library, so there is no need to provide
`sapi::Sandbox::GetLibPath()` nor `sapi::Sandbox::GetPolicy()` methods.

The zlib SAPI can be found in [//sapi_sandbox/examples/zlib](../examples/zlib),
along with its [host code](../examples/zlib/main_zlib.cc).
