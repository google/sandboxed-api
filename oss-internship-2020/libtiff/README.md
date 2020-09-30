# sapi-libtiff
Copyright 2020 Google LLC.

## Start use
You should make sure the libtiff submodule is cloned.

`git clone --recursive https://github.com/alexelex/sapi-libtiff`

## Usage

#### build:
`mkdir -p build && cd build && cmake .. -DSAPI_ROOT=/path/to/sapi_root -DBUILD_SHARED_LIBS=OFF`

`make -j 8`

#### to run the sandboxed example:
`./example/sandboxed /absolute/path/to/project/dir`

#### to run tests:
`./test/tests`
