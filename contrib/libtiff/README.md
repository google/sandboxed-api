# sapi-libtiff

This library was sandboxed as part of Google's summer 2020 internship program
([blog post](https://security.googleblog.com/2020/12/improving-open-source-security-during.html)).

Copyright 2020 Google LLC.

## Usage

#### build:

```bash
mkdir -p build && cd build && cmake .. \
  -DSAPI_ROOT=$HOME/sapi_root \
  -DBUILD_SHARED_LIBS=OFF
make -j 8
```

#### to run the sandboxed example:

`./example/sandboxed absolute/path/to/project/dir`

#### to run tests:

`./test/tests`

you also can use sandbox flags `sandbox2_danger_danger_permit_all` and
`sandbox2_danger_danger_permit_all_and_log` for debugging.
