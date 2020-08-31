# Guetzli Sandbox
This is an example implementation of a sandbox for the [Guetzli](https://github.com/google/guetzli) library using [Sandboxed API](https://github.com/google/sandboxed-api).
Please read Guetzli's [documentation](https://github.com/google/guetzli#introduction) to learn more about it.

## Implementation details
Because Guetzli provides a C++ API and SAPI requires functions to be `extern "C"`, a wrapper library has been written for the compatibility. SAPI provides a Transaction class, which is a convenient way to create a wrapper for your sandboxed API that handles internal errors. The original Guetzli has a command-line utility to encode images, so a fully compatible utility that uses sandboxed Guetzli is provided.

The wrapper around Guetzli uses file descriptors to pass data to the sandbox. This approach restricts the sandbox from using the `open()` syscall and also helps to prevent making copies of data, because you need to synchronize it between processes.

## Build Guetzli Sandboxed
Right now Sandboxed API support only Linux systems, so you need one to build it. Guetzli sandboxed uses [Bazel](https://bazel.build/) as a build system so you need to [install it](https://docs.bazel.build/versions/3.4.0/install.html) before building.

To build Guetzli sandboxed encoding utility you can use this command:
`bazel build //:guetzli_sandboxed`

Then you can use it in this way:
```
guetzli_sandboxed [--quality Q] [--verbose] original.png output.jpg
guetzli_sandboxed [--quality Q] [--verbose] original.jpg output.jpg
```
Refer to Guetzli's [documentation](https://github.com/google/guetzli#using) to read more about usage.

## Examples
There are two different sets of unit tests which demonstrate how to use different parts of Guetzli sandboxed:
* `tests/guetzli_sapi_test.cc` - example usage of Guetzli sandboxed API.
* `tests/guetzli_transaction_test.cc` - example usage of Guetzli transaction.

To run tests use the following command:
`bazel test ...`

Also, there is an example of custom security policy for your sandbox in
`guetzli_sandbox.h`
