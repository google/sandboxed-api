# Jsonnet Sandboxed API

This library provides sandboxed version of the [Jsonnet](https://github.com/google/jsonnet) library. 

## Examples

For now the only example command-line tool `jsonnet_sandboxed` enables the user to evaluate jsonnet code held in one file and writing to one output file. The tool is based on what can be found [here](https://github.com/google/jsonnet/blob/master/cmd/jsonnet.cpp) -- .

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
For now it supports evaluating one input file (possibly relying on multiple other files, e.x. by jsonnet `import` command; the files must be held in the same directory as input file) into one output file. Example jsonnet codes to evaluate can be found [here](https://github.com/google/jsonnet/tree/master/examples).
