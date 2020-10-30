# sandboxed LibPNG
Copyright 2020 Google LLC.

## Start use
You should make sure the libtiff submodule is cloned.

`git clone --recursive https://github.com/google/sandboxed-api`

## Usage

#### Build:
```
mkdir -p build && cd build && \
	cmake .. -DSAPI_ROOT=/path/to/sapi_root
make -j8
```

#### Example:
You should add `-DLIBPNG_SAPI_ENABLE_EXAMPLES=ON` to use the example.\
run PNG to PNG: `./examples/pngtopng /absolute/path/to/input/image.png /absolute/path/to/output/image.png`\
run RGB to BGR: `./examples/rgbtobgr /absolute/path/to/input/image.png /absolute/path/to/output/image.png`

Input and output examples can be found in images directory.

PNG to PNG: \
input: `/abs/path/to/project/images/pngtest.png`\
output:` /abs/path/to/project/images/pngtopng_pngtest.png`

RGB to BGR: \
input: `/abs/path/to/project/images/red_ball.png`\
output:` /abs/path/to/project/images/rgbtobgr_red_ball.png`


#### Tests:
You should add `-DLIBPNG_SAPI_ENABLE_TESTS=ON` to use tests.\
run: `./tests/tests`
