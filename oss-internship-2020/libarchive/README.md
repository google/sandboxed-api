# libarchive Sandboxed API

Sandboxed version of the [libarchive](https://www.libarchive.org/) minitar [example](https://github.com/libarchive/libarchive/blob/master/examples/minitar/minitar.c) using [Sandboxed API](https://github.com/google/sandboxed-api).

## Build

```
mkdir -p build && cd build
cmake .. -G Ninja
cmake --build .
```

The example binary file can be found at **build/examples/sapi_minitar** and the unit tests at **build/test/sapi_minitar_test**.

## Patches

The original libarchive code required patching since one of the custom types produced errors with libclang Python byndings. The patches are applied automatically during the build step and they do not modify the functionality of the library. The repository is also fetched automatically.

## Examples

In this project, the minitar example is sandboxed.
The code is found in the **examples** directory and is structured as follows:
- **sapi_minitar_main.cc** - ***main*** function of the minitar tool. This is mostly similar to the original example.
- **sapi_minitar.h** and **sapi_minitar.cc** - The two main functions (***CreateArchive*** and ***ExtractArchive***) and other helper functions.
- **sandbox.h** - Custom security policies, depending on the whether the user creates or extracts an archive.

On top of that, unit tests can be found in the **test/minitar_test.cc** file.

## Usage

The unit tests can be executed with `./build/test/sapi_minitar_test`.

The **sapi_minitar** command line tool can be used in the same way as the original example. It is also similar to the [tar](https://man7.org/linux/man-pages/man1/tar.1.html) command, only with fewer options:

`./build/examples/sapi_minitar -[options] [-f file] [files]`

The available options are:
- *c* - Create archive.
- *x* - Extract archive.
- *t* - Extract archive but only print entries.
- *p* - Preserve.
- *v* - Verbose.
- *j* or *y* - Compress with BZIP2.
- *Z* - Default compression.
- *z* - Compress with GZIP.

If no compression method is chosen (in the case of archive creation) the files will only be stored.

