
# GDAL Raster to GeoTIFF Workflow Sandbox
This repository is an example of how Sandboxed API can be used with GDAL C Raster API to implement the creation of the GeoTIFF dataset inside the sandbox.

## Workflow details
Implemented workflow consists of a few steps:
1. Register needed drivers inside the sandbox
2. Get specific driver by name (GTiff)
3. Map output file inside the sandbox and create a GeoTIFF dataset backed by this file
4. Set affine transformation coefficients if needed
5. Set projection reference string if needed
6. Write raster bands data to the dataset using RasterIO
  1. Set No data value if needed
7. Clean up data and close the dataset

## Implementation details
This project consists of a CMake file that shows how you can connect Sandboxed API and GDAL, a raster data parser using unsandboxed GDAL to generate sample input for the sandboxed workflow, sample sandbox policy that could work with GeoTIFF files without any violations, command-line utility that uses sandboxed GDAL to implement the workflow and GoogleTest unit tests to compare raster data of original datasets with the raster data of datasets that have been created inside the sandbox.

## Build GDAL sandbox
To build a GDAL sandbox, it's required to have a static build of libgdal and libproj. Moreover, proj.db file path is required to be able to map it inside the sandbox and use it internally for some of the projections.

### Build GDAL and PROJ from sources
To get the latest version of both GDAL and PROJ you will need to build them from sources.
First, you should build PROJ with this [tutorial](https://proj.org/install.html#compilation-and-installation-from-source-code).
After the installation, you should have a static build of libproj, remember the path as you will need to specify it later in CMake build.
Then, get gdal sources using git submodules:
`git submodule add https://github.com/OSGeo/gdal/`
`git submodule update --init --recursive`
After that you can go to the GDAL sources and make a static build of libgdal:
`cd gdal/gdal`
`./configure --with_proj=/path/to/proj/` 
`make static-lib`
**Note**: On the `./configure` step you should specify the path to your proj library as a `--with-proj=` argument to make everything work correctly.
### Build GDAL using dev-packages
### Build sandboxed GDAL
To build the examples from this repository you can use CMake in the following way:
```
mkdir build
cd build
cmake .. -G Ninja -DSAPI_ROOT=/path/to/sapi
```
This build expects `lib/` folder with both `libgdal.a` and `libproj.a` to be present near the source files.
Also, you need to have `gdal.h` header so Sandboxed API generator could parse it, the default expected path to it is `/usr/local/include`.
Finally, you could enable tests with the `-DENABLE_TESTS=ON` option for the CMake.
You can specify those paths as a CMake argument, so the complete example looks like this:
```
mkdir build
cd build
cmake .. -G Ninja -DSAPI_ROOT=/path/to/sapi    \
-DGDAL_HEADER_PREFIX=/path/to/gdal/header      \
-DLIBGDAL_PREFIX=/path/to/libgdal_static_build \
-DLIBPROJ_PREFIX=/path/to/libproj_static_build \
-DENABLE_TESTS=ON
```
After CMake build completed you can run `ninja` to build executables.

## Examples
Before running any of the examples, you need to specify the path to the `proj.db` using the environment variable.
To do so, run `export PROJ_PATH=/path/to/proj.db`. Alternatively, if there is no such environment variable program will try to use the default path `/usr/local/share/proj/proj.db`.
There is a simple command-line utility that takes path to the GeoTIFF file and absolute path to the output file as arguments, parses raster data from the input file and, re-creates the same GeoTIFF file (except some metadata) inside the sandbox.
You can run it in the following way:
`./raster_to_gtiff path/to/input.tif /absolute/path/to/output.tif`
After that, you can compare both files using the `gdalinfo` utility.
Also, there are unit tests that automatically convert a few files and then compare input and output raster data to make sure that they are equal.
To run tests your CMake build must use `-DENABLE_TESTS=ON`, then you can run tests using `./tests`.

All test data is from [osgeo samples](http://download.osgeo.org/geotiff/samples/).
