# Getting started with SAPI

## Build Dependencies

To build and run code with SAPI, the following dependencies must be installed
on the system:

* To compile your code: GCC 6 (version 7 or higher preferred) or Clang 7 (or
  higher)
* For auto-generating header files: Clang Python Bindings
* [Bazel](https://bazel.build/) version 0.23.0
* Python 2.7 with type annotations
* Linux userspace API headers

On a system running Debian 10 "Buster", these commands will install the
necessary packages:

```bash
echo "deb http://storage.googleapis.com/bazel-apt stable jdk1.8" | \
  sudo tee /etc/apt/sources.list.d/bazel.list
wget -qO - https://bazel.build/bazel-release.pub.gpg | sudo apt-key add -
sudo apt-get install -qy python-typing python-clang-7 libclang-7-dev
sudo apt-get install -qy build-essential linux-libc-dev bazel
```

Please refer to the
[Bazel documentation](https://docs.bazel.build/versions/master/bazel-overview.html)
for information on how to change the default compiler toolchain.


## Examples

Under [Examples](examples.md) you can find a few libraries, previously prepared
by the SAPI team.


## Development Process

You will have to prepare two parts of your a sandbox library project. The
sandboxed library part (**SAPI library**), and the **host code**
which will make use of functionality exposed by your sandboxed library.


## SAPI Library

The *SAPI library* is a sandboxed process, which exposes required functionality
to the *host code*.

In order to create it, you'll need your C/C++ library, for example another open
source project on GitHub. You will also have to create some supporting code
(part of it will be automatically generated). This code will describe which
functionality exactly you would like to contain (which library functions), and
the [sandbox policies](../sandbox2/docs/getting-started.md#policy) you would
like your library to run under.

All those steps are described in details under [Library](library.md).


## Host Code

The *host code* is making use of functions exported by your *SAPI Library*.

It makes calls to sandboxed functions, receives results, and can access memory
of a *SAPI library* in order to make copies of remote variables and memory
blocks (arrays, structures, protocol buffers, etc.). Those memory blocks
can then be accessed by the local process.

The host code can also copy contents of local memory to the remote process if
needed.

Read about writing host code [here](host-code.md).
