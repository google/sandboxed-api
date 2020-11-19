# Sandboxing the minitar example (only the extraction function) using LD_PRELOAD

## Description
Using `LD_PRELOAD` we can create our own functions with the same signatures as the ones from the libarchive that are called in the original code and call those instead.
This example only implements the extract part of the original tool.

## Structure
- **minitar_main.cc** - the original main function.
- **minitar.cc** and **minitar.h** - original functions called from main, built into a shared library.
- The three files mentioned above are taken from the original minitar [example](https://github.com/libarchive/libarchive/tree/master/examples/minitar). The only difference to the code is that the main function was separated from the rest in order to build a shared library so that when `LD_PRELOAD` is used we can call our custom functions.
- **sapi_minitar.cc** - libarchive functions with our own implementation, built into a shared library which will be used with `LD_PRELOAD`.

Most of the functions simply convert the arguments to sapi::v objects and calls the sandboxed version of the function. Also, there is a custom *extract* implementation that also created the sandbox object. If the sandbox could be created without special arguments then this would not be needed and could be done inside of the first function called. However, in our case, the sandbox requires the file path and so we do this in the *create* function where we have access to that path. Aftert this, we call the original function (the "next" function symbol).

## Files changed from the original libarchive sandboxed version
The only changes are in the root CMakeLists.txt:

- `add_subdirectory(ld_preload_example)` to add this current folder to the build process.

- `set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)` to make sure that shared libraries are built with position independent code.

## Usage
`LD_PRELOAD= build/ld_preload_example/libminitar_preload.so build/ld_preload_example/minitar_original -xvf archive_file`

Instead of the *x* option, *t* can be used as well only to print the archive entries.
