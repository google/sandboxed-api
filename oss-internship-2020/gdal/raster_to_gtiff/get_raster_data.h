// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef RASTER_TO_GTIFF_GET_RASTER_DATA_H_
#define RASTER_TO_GTIFF_GET_RASTER_DATA_H_

#include <optional>
#include <string>
#include <vector>

namespace gdal::sandbox::parser {

struct RasterBandData {
  int width;
  int height;
  std::vector<int> data;
  int data_type;     // Corresponds to the GDALDataType enum
  int color_interp;  // Corresponds to the GDALColorInterp enum
  std::optional<double> no_data_value;
};

struct RasterDataset {
  int width;
  int height;
  std::vector<RasterBandData> bands;
  std::string wkt_projection;  // OpenGIS WKT format
  std::vector<double> geo_transform;
};

RasterDataset GetRasterBandsFromFile(const std::string& filename);
bool operator==(const RasterBandData& lhs, const RasterBandData& rhs);
bool operator==(const RasterDataset& lhs, const RasterDataset& rhs);

}  // namespace gdal::sandbox::parser

#endif  // RASTER_TO_GTIFF_GET_RASTER_DATA_H_
