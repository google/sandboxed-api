# Host Code

## Description

The *host code* is the actual code making use of the functionality offered by
its contained/isolated/sandboxed counterpart, i.e. a [SAPI Library](library.md).

Such code implements the logic, that any program making use of a typical library
would: it calls functions exported by said library, passing and receiving data
to/from it.

Given that the SAPI Library lives in a separate and contained/sandboxed process,
calling such functions directly is not possible. Therefore the SAPI project
provides tools which create an API object that proxies accesses to sandboxed
libraries.

More on that can be found under [library](library.md).


## Variables

In order to make sure that host code can access variables and memory blocks in
a remote process, SAPI provides a comprehensive set of C++ classes. These try to
make the implementation of the main logic code simpler. To do this you will
sometimes have to use those objects instead of typical data types known from C.

For example, instead of an array of three `int`'s, you will instead have to use
and pass to sandboxed functions the following object
```cpp
  int arr[3] = {1, 2, 3};
  sapi::v::Array<int> sarr(arr, ABSL_ARRAYSIZE(arr));
```

[Read more](variables.md) on the internal data representation used in host
code.


## Transactions

When you use a typical library of functions, you do not have to worry about the
fact that a call to a library might fail at runtime, as the linker ensures all
necessary functions are available after compilation.

Unfortunately with the SAPI, the sandboxed library lives in a separate process,
therefore we need to check for all kinds of problems related to passing such
calls via our RPC layer.

Users of SAPI need to check - in addition to regular errors returned by the
native API of a library - for errors returned by the RPC layer. Sometimes these
errors might not be interesting, for example when doing bulk processing and you
would just restart the sandbox.

Handling these errors would mean that each call to a SAPI library is followed
by an additional check to RPC layer of SAPI. To make handling of such
cases easier we have implemented the `::sapi::Transaction` class.

This module makes sure that all function calls to the sandboxed library were
completed without any RPC-level problems, or it will return relevant error.

Read more about this module under [Transactions](transactions.md).


## Sandbox restarts

Many sandboxees handle sensitive user input. This data might be at risk when the
sandboxee was corrupted at some point and stores data between runs - imagine
an Imagemagick sandbox that starts sending out pictures of the previous run. To
avoid this we need to stop reusing sandboxes. This can be achieved by restarting
the sandboxee with `::sapi::Sandbox::Restart()` or
`::sapi::Transaction::Restart()` when using transactions.

**Restarting the sandboxee will invalidate any references to the sandboxee!**
This means passed file descriptors/allocated memory will not exist anymore.

Note: Restarting the sandboxee takes some time, about *75-80 ms* on modern
machines (more if network namespaces are used).
