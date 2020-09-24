# GDAL Raster GeoTIFF Workflow

```
Build Tools: CMake/Ninja
OS: Linux
```

### For installing GDAL:

```
sudo apt-get install python3.6-dev
sudo add-apt-repository ppa:ubuntugis/ppa && sudo apt update
sudo apt-get install gdal-bin
sudo apt-get install libgdal-dev
```

### Dependencies:

PNG: `sudo apt-get install libpng-dev`

PCRE: `sudo apt-get install libpcre3 libpcre3-dev`

PROJ: `sudo apt-get install libproj-dev`

OBS! You may need to set `export LD_LIBRARY_PATH=/lib:/usr/lib:/usr/local/lib`.
It is required for libproj.so to be found into /usr/local/lib/. You can also fix
this by typing `locate libproj.so` which will give you
<the_absolute_libproj.so_path> and then `cp <the_absolute_libproj.so_path>
/usr/local/lib/`.

### Initializing GDAL submodule:

`git submodule add https://github.com/OSGeo/gdal/tree/master/gdal`

### Building GDAL statically

GNUmakefile from gdal/gdal can handle building the static library.

`cd gdal/gdal && make static-lib`

`cd ../.. && mkdir lib`

`cp gdal/gdal/libgdal.a lib/`

OBS! The file is huge! It may take a while.

### For testing:

`mkdir build && cd build`

`cmake .. -G Ninja`

`ninja`

`./raster <your_absolute_tiff_file_path>`

## About the project

GDAL is a translator library for raster and vector geospatial data format. The
project consist in rastering a GeoTIFF file format using GDAL functionalities
and sandboxed methods.

## Implementation

*Sandboxing...*

The purpose of sandboxing is to limit the permissions and capabilities of
library’s methods, in order to secure the usage of them. After obtaining the
sandbox, the functions will be called through an Sandbox API (being called api
in the current test) and so, the operations, system calls or namspaces access
may be controlled.

*Raster process...*

Useful functions from the `gdal.h` header are added to the SAPI library built
with CMake.

One .tiff file is manipulated with GDALOpen functionality, which extracts a
pointer to the data set containg a list of raster bands, all pertaining to the
same area. Metadata, a coordinate system, a georeferencing transform, size of
raster and various other information are kept into the data set that corresponds
to the image.

To create an array containing the image information, the dimentions needed are
extracted using some specific GDAL(X/Y)Size functions applied to the block.
GDALRasterBand function takes care of data type conversion, one more step
following: placing the converted data (with RasterIO method) into the created
and well allocated structure.
