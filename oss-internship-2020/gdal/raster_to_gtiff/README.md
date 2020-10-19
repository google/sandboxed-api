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
Because GDAL doesn't use CMake or Bazel it's required to have a static build of libgdal and libproj. Moreover, proj.db file path is required to be able to map it inside the sandbox and use it internally for some of the projections.

### Build GDAL and PROJ from sources
To get the latest version of both GDAL and PROJ you will need to build them from sources. To make a clean installation you can use the build folder as an installation path, with this approach you won't affect any system files and will be able to easily delete everything later.
First, you will need to build PROJ, which is used internally in the GDAL. You can't use the libproj-dev package because it contains an outdated version, while GDAL requires a more recent one.
To [install PROJ from sources](https://proj.org/install.html#compilation-and-installation-from-source-code) you can do the following:
```
mkdir build && cd build
wget https://download.osgeo.org/proj/proj-7.1.1.tar.gz
tar xvzf proj-7.1.1.tar.gz
mkdir proj_build
cd proj-7.1.1
./configure --prefix=/path/to/build/proj_build
make -j8
make install
make check
```
The static version of libproj will be available at `proj_build/lib/libproj.a`.
Then, you can start [GDAL installation](https://trac.osgeo.org/gdal/wiki/BuildingOnUnix):
```
cd build
git clone https://github.com/OSGeo/gdal
mkdir gdal_build
cd gdal/gdal
./configure --prefix=/path/to/build/gdal_build --with-proj=/path/to/build/proj_build
make -j8
make install
```
To verify that everything is installed correctly you can run gdalinfo utility.
```
cd ../../gdal_build/bin/
./gdalinfo --version
```
The static version of libgdal will be available at `gdal_build/lib/libgdal.a`.
You will need to specify paths to those static libraries as the CMake arguments to build the project. Also, the Sandboxed API generator needs `gdal.h` header, which is located at `build/gdal/gdal/gcore/gdal.h`.

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
After CMake build is completed, you can run `ninja` to build the executables.

## Examples
PROJ uses `proj.db` database to work correctly with different transformations and you will need to map it to the sandbox manually to be able to retrieve it in the restricted environment. After the installation you can find `proj.db` in `/path/to/proj/share/proj/proj.db`.

You can use environment variables to set path to proj:
```
export PROJ_DB_PATH=/path/to/proj.db
```
The code will check this variable and if it represents a valid file it will be mounted inside the sandbox.
Alternatively, if there is no such environment variable program will try to use the default path `/usr/local/share/proj/proj.db`.
There is a simple command-line utility that takes path to the GeoTIFF file and absolute path to the output file as arguments, parses raster data from the input file and, re-creates the same GeoTIFF file (except some metadata) inside the sandbox.

You can run it in the following way:
```
./raster_to_gtiff path/to/input.tif /absolute/path/to/output.tif
```
After that, you can compare both files using the `gdalinfo` utility.
Also, there are unit tests that automatically convert a few files and then compare input and output raster data to make sure that they are equal.
To run tests your CMake build must use `-DENABLE_TESTS=ON`, then you can run tests using `ctest`.
Note that it will also run Sandboxed API related tests. To run tests manually you will need to specify a few environmental variables and then run `tests` executable.
```
export TEST_TMPDIR=/tmp/
export TEST_SRCDIR=/path/to/project/source
```

All test data is from [osgeo samples](http://download.osgeo.org/geotiff/samples/).
