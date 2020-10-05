# GDAL Raster to GeoTIFF Workflow Sandbox
This repository is an example of how Sandboxed API can be used with GDAL C Raster API to implement the creation of the GeoTIFF dataset inside the sandbox.

## Workflow details
Implemented workflow consists of a few steps:
1. Register needed drivers inside the sandbox
2. Get specific driver by name (GTiff)
3. Map output file inside the sandbox and create GeoTIFF dataset backed by this file
4. Set affine transformation coefficients if needed
5. Set projection reference string if needed
6. Write raster bands data to the dataset using RasterIO
  1. Set No data value if needed
7. Clean up data and close the dataset

## Implementation details

## Build details

### Build GDAL and PROJ from sources

## Examples