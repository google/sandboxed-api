# libxls Sandboxed API

This directory contains a ready-made sandbox for the
[libxls](https://github.com/libxls/libxls) library.

The libxls library uses Autotools and a special macro package for its
configuration. This means that on Debian based systems, the package
`autoconf-archive` needs to be installed or the configuration phase
will fail with the following error:

```
./configure: line 16126: syntax error near unexpected token `,'
./configure: line 16126: `AX_CXX_COMPILE_STDCXX_11(, optional)'
```
