# Jsonnet Sandboxed API

This library provides sandboxed version of the [Jsonnet](https://github.com/google/jsonnet) library. 

## Examples

The `examples/` directory contains code to produce three command-line tools -- `jsonnet_sandboxed`, `jsonnet_yaml_stream_sandboxed` and `jsonnet_multiple_files_sandboxed` to evaluate jsonnet code. The first one enables the user to evaluate jsonnet code held in one file and writing to one output file. The second evaluates one jsonnet file into one file, which can be interepreted as YAML stream. The third one is for evaluating one jsonnet file into multiple output files.
All three tools are based on what can be found [here](https://github.com/google/jsonnet/blob/master/cmd/jsonnet.cpp).

Apart from these, there is also a file producing `jsonnet_formatter_sandboxed` executable. It is based on a tool found from [here](https://github.com/google/jsonnet/blob/master/cmd/jsonnetfmt.cpp). It is a jsonnet code formatter -- it changes poorly written jsonnet files into their canonical form.

## Build

To build these examples, after cloning the whole Sandbox API project, you also need to run

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
To run `jsonnet_sandboxed` (or `jsonnet_yaml_stream_sandboxed` or `jsonnet_formatter_sandboxed` in a similar way):
```
cd examples
./jsonnet_sandboxed \
    absolute/path/to/the/input_file.jsonnet \ 
    absolute/path/to/the/output_file
```
To run `jsonnet_mutiple_files_sandboxed`:
```
cd examples
./jsonnet_mutiple_files_sandboxed \
    absolute/path/to/the/input_file.jsonnet \ 
    absolute/path/to/the/output_directory
```

All three tools support evaluating one input file (possibly relying on multiple other files, e.x. by jsonnet `import` command; the files must be held in the same directory as input file) into one or more output files. Example jsonnet codes to evaluate in a one-in-one-out manner can be found [here](https://github.com/google/jsonnet/tree/master/examples). Example code producing multiple output files or YAML stream files can be found in the `examples/jsonnet_codes` directory (along with some other examples copied with minimal changes from the library files), in files called `multiple_files_example.jsonnet` and `yaml_stream_example.jsonnet`, respectively. In the `examples/jsonnet_codes_expected_output` directory one can found outputs the mentioned above files' evaluation should produce.

The formatter reads one input file and produces one output file as a result. Example code for this tool can also be found in `examples/jsonnet_codes` directory, in a file called `formatter_example.jsonnet`.

## Testing

A few tests prepared with a use of [Google Test](https://github.com/google/googletest) framework can be found in the `tests/` directory. To run them type:
```
cd tests
./tests
```
