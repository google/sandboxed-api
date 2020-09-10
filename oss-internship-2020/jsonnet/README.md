# Jsonnet Sandboxed API

This library provides sandboxed version of the [Jsonnet](https://github.com/google/jsonnet) library. 

## Examples

The `examples/` directory contains code to produce two command-line tools -- `jsonnet_sandboxed` and `jsonnet_multiple_files_sandboxed`. The first one enables the user to evaluate jsonnet code held in one file and writing to one output file. The other one is for evaluating one jsonnet file into multiple output files.
Both tools are based on what can be found [here](https://github.com/google/jsonnet/blob/master/cmd/jsonnet.cpp).

## Build

To build this example, after cloning the whole Sandbox API project, you also need to run

```
git submodule update --init --recursive
```
anywhere in the project tree in order to clone the `jsonnet` submodule.
Then in the `sandboxed-api/oss-internship-2020/jsonnet` run
```
mkdir build && cd build
cmake -G Ninja
ninja
```
To run `jsonnet_sandboxed`:
```
cd examples
./jsonnet_sandboxed absolute/path/to/the/input_file.jsonnet \ 
    absolute/path/to/the/output_file
```
To run `jsonnet_mutiple_files_sandboxed`:
```
cd examples
./jsonnet_mutiple_files_sandboxed absolute/path/to/the/input_file.jsonnet \ 
    absolute/path/to/the/output_directory
```
Both tools support evaluating one input file (possibly relying on multiple other files, e.x. by jsonnet `import` command; the files must be held in the same directory as input file) into one or more output files. Example jsonnet codes to evaluate in a one-in-one-out manner can be found [here](https://github.com/google/jsonnet/tree/master/examples). Example code producing multiple output files can be found in the `examples` directory, in a file called `multiple_files_example.jsonnet`.