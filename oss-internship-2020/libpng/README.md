# sandboxed LibPNG
Copyright 2020 Google LLC.

## Start use
You should make sure the libtiff submodule is cloned.

`git clone --recursive https://github.com/google/sandboxed-api`

## Usage

#### Build:
```
mkdir -p build && cd build
cmake .. -DSAPI_ROOT=/path/to/sapi_root
make -j8
```

#### Example:
You should add `-DLIBPNG_SAPI_BUILD_EXAMPLES=ON` to use the example.\
run PNG to PNG:
```
./examples/pngtopng /absolute/path/to/input/image.png /absolute/path/to/output/image.png
```
run RGB to BGR:
```
./examples/rgbtobgr /absolute/path/to/input/image.png /absolute/path/to/output/image.png
```

Examples of input and output can be found in `images`.

PNG to PNG: \
input: `images/pngtest.png`\
output:` images/pngtopng_pngtest.png`

RGB to BGR: \
input: `images/red_ball.png`\
output: `images/rgbtobgr_red_ball.png`


#### Tests:
You should add `-DLIBPNG_SAPI_BUILD_TESTING=ON` to use tests and do:
```
cd tests
ctest .
```
