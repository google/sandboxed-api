# GDAL Raster GeoTIFF Workflow

```
Build Tools: CMake/Ninja
OS: Linux
```

### For testing: 
`mkdir build && cd build`

`cmake .. -G Ninja`

`ninja`

`./raster` 

## About the project
    GDAL is a translator library for raster and vector 
    geospatial data format. 
    The project consist in rastering a GeoTIFF file format 
    using GDAL functionalities and sandboxed methods. 

## Implementation
    
*Sandboxing...*

    The purpose of sandboxing is to limit the permissions 
    and capabilities of library’s methods, in order to 
    secure the usage of them. After obtaining the sandbox, 
    the functions will be called through an Sandbox API 
    (being called api in the current test) and so, the 
    operations, system calls or namspaces access may be 
    controlled. 

*Raster process...*

    From gdal.h header useful methods are added to sapi 
    library builded with CMake. 

    One .tiff file is manipulated with GDALOpen 
    functionality, which extracts a pointer to the data set
    containg a list of raster bands, all pertaining to the
    same area. 
    Metadata, a coordinate system, a georeferencing
    transform, size of raster and various other information
    are kept into the data set that corresponds to the image.

    To create an array containing the image information, the
    dimentions needed are extracted using some specific 
    GDAL(X/Y)Size functions applied to the block.
    GDALRasterBand function takes care of data type conversion, one more step following: placing the
    converted data (with RasterIO method) into the created
    and well allocated structure.