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

#include "get_raster_data.h"  // NOLINT(build/include)

#include <memory>

#include "gdal.h"  // NOLINT(build/include)

namespace gdal::sandbox::parser {

namespace {

inline constexpr int kGeoTransformSize = 6;

}  // namespace

RasterDataset GetRasterBandsFromFile(const std::string& filename) {
  GDALAllRegister();
  GDALDatasetH dataset = GDALOpen(filename.data(), GA_ReadOnly);
  GDALDriverH driver = GDALGetDatasetDriver(dataset);

  RasterDataset result_dataset = {GDALGetRasterXSize(dataset),
                                  GDALGetRasterYSize(dataset)};

  if (GDALGetProjectionRef(dataset) != nullptr) {
    result_dataset.wkt_projection = std::string(GDALGetProjectionRef(dataset));
  }

  std::vector<double> geo_transform(kGeoTransformSize, 0.0);

  if (GDALGetGeoTransform(dataset, geo_transform.data()) == CE_None) {
    result_dataset.geo_transform = std::move(geo_transform);
  }

  int bands_count = GDALGetRasterCount(dataset);

  std::vector<RasterBandData> bands_data;
  bands_data.reserve(bands_count);

  for (int i = 1; i <= bands_count; ++i) {
    GDALRasterBandH band = GDALGetRasterBand(dataset, i);
    int width = GDALGetRasterBandXSize(band);
    int height = GDALGetRasterBandYSize(band);

    std::unique_ptr<int> no_data_result = nullptr;
    double no_data_value = GDALGetRasterNoDataValue(band, no_data_result.get());
    std::optional<double> no_data_value_holder =
        no_data_result.get() == nullptr
            ? std::nullopt
            : std::make_optional<double>(no_data_value);

    int data_type = static_cast<int>(GDALGetRasterDataType(band));
    int color_interp = static_cast<int>(GDALGetRasterColorInterpretation(band));

    std::vector<int32_t> band_raster_data(width * height);

    // GDALRasterIO with GF_Write should use the same type (GDT_Int32)
    GDALRasterIO(band, GF_Read, 0, 0, width, height, band_raster_data.data(),
                 width, height, GDT_Int32, 0, 0);

    bands_data.push_back({width, height, std::move(band_raster_data), data_type,
                          color_interp, std::move(no_data_value_holder)});
  }

  result_dataset.bands = std::move(bands_data);

  GDALClose(dataset);

  return result_dataset;
}

bool operator==(const RasterBandData& lhs, const RasterBandData& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height &&
         lhs.data == rhs.data && lhs.data_type == rhs.data_type &&
         lhs.color_interp == rhs.color_interp &&
         lhs.no_data_value == rhs.no_data_value;
}

bool operator==(const RasterDataset& lhs, const RasterDataset& rhs) {
  return lhs.width == rhs.width && lhs.height == rhs.height &&
         lhs.bands == rhs.bands && lhs.wkt_projection == rhs.wkt_projection &&
         lhs.geo_transform == rhs.geo_transform;
}

}  // namespace gdal::sandbox::parser
