# OpenJPEG Sandboxed API

This library provides sandboxed version of the [OpenJPEG](https://github.com/uclouvain/openjpeg) library.

## Examples

The examples are sandboxed and simplified version of the main tools provided by the OpenJPEG library, namely (for now) `opj_decompress` from [here](https://github.com/uclouvain/openjpeg/blob/master/src/bin/jp2/opj_decompress.c).

In `decompress_example.cc` the library's sandboxed API is used to convert the _.jp2_ to _.pnm_ image format.

## Build

To build this example, after cloning the whole Sandbox API project, you also need to run

```
git submodule update --init --recursive
```
anywhere in the project tree in order to clone the `openjpeg` submodule.
Then in the `sandboxed-api/oss-internship-2020/openjpeg` run
```
mkdir build && cd build
cmake -G Ninja
ninja
```
To run `decompress_sandboxed`:
```
cd examples
./decompress_sandboxed absolute/path/to/the/file.jp2 absolute/path/to/the/file.pnm
```
