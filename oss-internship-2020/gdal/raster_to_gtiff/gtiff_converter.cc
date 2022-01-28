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

#include "gtiff_converter.h"  // NOLINT(build/include)

#include "sandboxed_api/util/fileops.h"

namespace gdal::sandbox {

namespace {

inline constexpr absl::string_view kDriverName = "GTiff";

}  // namespace

RasterToGTiffProcessor::RasterToGTiffProcessor(std::string out_file_full_path,
                                               std::string proj_db_path,
                                               parser::RasterDataset data,
                                               int retry_count)
    : sapi::Transaction(std::make_unique<GdalSapiSandbox>(
          sandbox2::file_util::fileops::StripBasename(out_file_full_path),
          std::move(proj_db_path))),
      out_file_full_path_(std::move(out_file_full_path)),
      data_(std::move(data)) {
  set_retry_count(retry_count);
  SetTimeLimit(absl::InfiniteDuration());
}

absl::Status RasterToGTiffProcessor::Main() {
  GdalApi api(sandbox());
  SAPI_RETURN_IF_ERROR(api.GDALAllRegister());

  sapi::v::CStr driver_name_ptr(kDriverName);

  SAPI_ASSIGN_OR_RETURN(absl::StatusOr<GDALDriverH> driver,
                        api.GDALGetDriverByName(driver_name_ptr.PtrBefore()));

  TRANSACTION_FAIL_IF_NOT(driver.value() != nullptr,
                          "Error getting GTiff driver");
  sapi::v::RemotePtr driver_ptr(driver.value());

  sapi::v::ConstCStr out_file_full_path_ptr(out_file_full_path_.c_str());
  sapi::v::NullPtr create_options;

  GDALDataType type = data_.bands.size() > 0
                          ? static_cast<GDALDataType>(data_.bands[0].data_type)
                          : GDALDataType::GDT_Unknown;

  SAPI_ASSIGN_OR_RETURN(
      absl::StatusOr<GDALDatasetH> dataset,
      api.GDALCreate(&driver_ptr, out_file_full_path_ptr.PtrBefore(),
                     data_.width, data_.height, data_.bands.size(), type,
                     &create_options));

  TRANSACTION_FAIL_IF_NOT(dataset.value(), "Error creating dataset");
  sapi::v::RemotePtr dataset_ptr(dataset.value());

  int current_band = 1;
  for (auto& band_data : data_.bands) {
    SAPI_ASSIGN_OR_RETURN(absl::StatusOr<GDALRasterBandH> band,
                          api.GDALGetRasterBand(&dataset_ptr, current_band));
    TRANSACTION_FAIL_IF_NOT(band.value() != nullptr,
                            "Error getting band from dataset");
    sapi::v::RemotePtr band_ptr(band.value());

    sapi::v::Array<int> data_array(band_data.data.data(),
                                   band_data.data.size());

    SAPI_ASSIGN_OR_RETURN(
        absl::StatusOr<CPLErr> result,
        api.GDALRasterIO(&band_ptr, GF_Write, 0, 0, band_data.width,
                         band_data.height, data_array.PtrBefore(),
                         band_data.width, band_data.height, GDT_Int32, 0, 0));

    TRANSACTION_FAIL_IF_NOT(result.value() == CPLErr::CE_None,
                            "Error writing band to dataset");

    SAPI_ASSIGN_OR_RETURN(
        result,
        api.GDALSetRasterColorInterpretation(
            &band_ptr, static_cast<GDALColorInterp>(band_data.color_interp)));

    TRANSACTION_FAIL_IF_NOT(result.value() == CPLErr::CE_None,
                            "Error setting color interpretation");

    if (band_data.no_data_value.has_value()) {
      SAPI_ASSIGN_OR_RETURN(result,
                            api.GDALSetRasterNoDataValue(
                                &band_ptr, band_data.no_data_value.value()));

      TRANSACTION_FAIL_IF_NOT(result.value() == CPLErr::CE_None,
                              "Error setting no data value for the band");
    }

    ++current_band;
  }

  if (data_.wkt_projection.length() > 0) {
    sapi::v::ConstCStr wkt_projection_ptr(data_.wkt_projection.c_str());
    SAPI_ASSIGN_OR_RETURN(
        absl::StatusOr<CPLErr> result,
        api.GDALSetProjection(&dataset_ptr, wkt_projection_ptr.PtrBefore()));
    TRANSACTION_FAIL_IF_NOT(result.value() == CPLErr::CE_None,
                            "Error setting wkt projection");
  }

  if (data_.geo_transform.size() > 0) {
    sapi::v::Array<double> geo_transform_ptr(data_.geo_transform.data(),
                                             data_.geo_transform.size());
    SAPI_ASSIGN_OR_RETURN(
        absl::StatusOr<CPLErr> result,
        api.GDALSetGeoTransform(&dataset_ptr, geo_transform_ptr.PtrBefore()));

    TRANSACTION_FAIL_IF_NOT(result.value() == CPLErr::CE_None,
                            "Error setting geo transform");
  }

  SAPI_RETURN_IF_ERROR(api.GDALClose(&dataset_ptr));

  return absl::OkStatus();
}

}  // namespace gdal::sandbox
