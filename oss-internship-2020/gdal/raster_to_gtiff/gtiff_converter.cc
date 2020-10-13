// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gtiff_converter.h"  // NOLINT(build/include)

namespace gdal::sandbox {

namespace {

  inline constexpr absl::string_view kDriverName = "GTiff";

}  // namespace

RasterToGTiffProcessor::RasterToGTiffProcessor(std::string out_file_full_path, 
                                                std::string out_file_folder,
                                                std::string proj_db_path,
                                                parser::RasterDataset data,
                                                int retry_count) 
  : sapi::Transaction(std::make_unique<GdalSapiSandbox>(
      std::move(out_file_folder), std::move(proj_db_path))),
    out_file_full_path_(std::move(out_file_full_path)),
    data_(std::move(data))
{
  set_retry_count(retry_count);
  SetTimeLimit(absl::InfiniteDuration());
}

absl::Status RasterToGTiffProcessor::Main() {
  gdalApi api(sandbox());
  SAPI_RETURN_IF_ERROR(api.GDALAllRegister());

  sapi::v::ConstCStr driver_name_ptr(kDriverName.data());

  SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<GDALDriverH> driver, 
      api.GDALGetDriverByName(driver_name_ptr.PtrBefore()));

  TRANSACTION_FAIL_IF_NOT(driver.value() != nullptr, 
      "Error getting GTiff driver");
  sapi::v::RemotePtr driver_ptr(driver.value());

  sapi::v::ConstCStr out_file_full_path_ptr(out_file_full_path_.c_str());
  sapi::v::NullPtr create_options;

  GDALDataType type = data_.bands.size() > 0 ?
    static_cast<GDALDataType>(data_.bands[0].data_type)
      : GDALDataType::GDT_Unknown;

  SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<GDALDatasetH> dataset, 
      api.GDALCreate(&driver_ptr, out_file_full_path_ptr.PtrBefore(), 
        data_.width, data_.height, data_.bands.size(), type, 
        &create_options));

  TRANSACTION_FAIL_IF_NOT(dataset.value(),
      "Error creating dataset");  
  sapi::v::RemotePtr dataset_ptr(dataset.value());

  for (int i = 0; i < data_.bands.size(); ++i) {
    SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<GDALRasterBandH> band,
        api.GDALGetRasterBand(&dataset_ptr, i + 1));
    TRANSACTION_FAIL_IF_NOT(band.value() != nullptr, 
        "Error getting band from dataset");
    sapi::v::RemotePtr band_ptr(band.value());

    sapi::v::Array<int> data_array(data_.bands[i].data.data(), 
        data_.bands[i].data.size());

    SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<CPLErr> result, 
        api.GDALRasterIO(&band_ptr, GF_Write, 0, 0,
          data_.bands[i].width, data_.bands[i].height, data_array.PtrBefore(),
          data_.bands[i].width, data_.bands[i].height, GDT_Int32, 0, 0));

    TRANSACTION_FAIL_IF_NOT(result.value() == CPLErr::CE_None, 
        "Error writing band to dataset");

    SAPI_ASSIGN_OR_RETURN(result, api.GDALSetRasterColorInterpretation(
        &band_ptr, static_cast<GDALColorInterp>(
              data_.bands[i].color_interp)));

    TRANSACTION_FAIL_IF_NOT(result.value() == CPLErr::CE_None,
        "Error setting color interpretation");

    if (data_.bands[i].no_data_value.has_value()) {
      SAPI_ASSIGN_OR_RETURN(result, 
          api.GDALSetRasterNoDataValue(&band_ptr, 
            data_.bands[i].no_data_value.value()));

      TRANSACTION_FAIL_IF_NOT(result.value() == CPLErr::CE_None, 
          "Error setting no data value for the band");
    }
  }

  if (data_.wkt_projection.length() > 0) {
    sapi::v::ConstCStr wkt_projection_ptr(data_.wkt_projection.c_str());
    SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<CPLErr> result, 
        api.GDALSetProjection(&dataset_ptr, wkt_projection_ptr.PtrBefore())); 
    TRANSACTION_FAIL_IF_NOT(result.value() == CPLErr::CE_None,
        "Error setting wkt projection");
  }

  if (data_.geo_transform.size() > 0) {
    sapi::v::Array<double> geo_transform_ptr(data_.geo_transform.data(), 
        data_.geo_transform.size());
    SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<CPLErr> result, 
        api.GDALSetGeoTransform(&dataset_ptr, geo_transform_ptr.PtrBefore()));

    TRANSACTION_FAIL_IF_NOT(result.value() == CPLErr::CE_None, 
        "Error setting geo transform");
  }

  SAPI_RETURN_IF_ERROR(api.GDALClose(&dataset_ptr));

  return absl::OkStatus();
}

}  // namespace gdal::sandbox
