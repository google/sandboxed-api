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

#include <iostream>
#include <cstdlib>
#include <vector>
#include <string>
#include <filesystem>
#include <optional>

#include "sandboxed_api/sandbox2/util/fileops.h"

#include "gdal_sandbox.h"
#include "get_raster_data.h"

namespace {

#define SAPI_FAIL_IF_NOT(x, y)               \
  if (!(x)) {                                \
    return absl::FailedPreconditionError(y); \
  }

inline constexpr absl::string_view kDriverName = "GTiff";
inline constexpr absl::string_view kProjDbEnvVariableName = "PROJ_PATH";
inline constexpr absl::string_view kDefaultProjDbPath 
    = "/usr/local/share/proj/proj.db";

std::optional<std::string> GetProjDbPath() {
  const char* proj_db_path_ptr = std::getenv(kProjDbEnvVariableName.data());

  std::string proj_db_path = proj_db_path_ptr == nullptr ? 
      std::string(kDefaultProjDbPath) : std::string(proj_db_path_ptr);

  if (!std::filesystem::exists(proj_db_path)) {
    return std::nullopt;
  }

  return proj_db_path;
}

absl::Status SaveToGTiff(gdal::sandbox::tests::parser::RasterDataset bands_data, 
    std::string out_file) {
  using namespace gdal::sandbox;

  std::string output_data_folder = "";

  if (!sandbox2::file_util::fileops::RemoveLastPathComponent(out_file,
      &output_data_folder)) {
    return absl::FailedPreconditionError("Error getting output file directory");
  }

  std::optional<std::string> proj_db_path = GetProjDbPath();
  SAPI_FAIL_IF_NOT(proj_db_path != std::nullopt,
      "Specified proj.db does not exist");

  GdalSapiSandbox sandbox(output_data_folder, std::move(proj_db_path.value()));
  SAPI_RETURN_IF_ERROR(sandbox.Init());
  gdalApi api(&sandbox);
  SAPI_RETURN_IF_ERROR(api.GDALAllRegister());

  sapi::v::ConstCStr driver_name_ptr(kDriverName.data());

  SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<GDALDriverH> driver, 
      api.GDALGetDriverByName(driver_name_ptr.PtrBefore()));

  SAPI_FAIL_IF_NOT(driver.value() != nullptr, 
      "Error getting GTiff driver");
  sapi::v::RemotePtr driver_ptr(driver.value());

  sapi::v::ConstCStr out_file_full_path_ptr(out_file.data());
  sapi::v::NullPtr create_options;

  GDALDataType type = bands_data.bands.size() > 0 ?
      static_cast<GDALDataType>(bands_data.bands[0].data_type)
        : GDALDataType::GDT_Unknown;

  SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<GDALDatasetH> dataset, 
      api.GDALCreate(&driver_ptr, out_file_full_path_ptr.PtrBefore(), 
        bands_data.width, bands_data.height, bands_data.bands.size(), type, 
        &create_options));

  SAPI_FAIL_IF_NOT(dataset.value(),
      "Error creating dataset");  
  sapi::v::RemotePtr dataset_ptr(dataset.value());

  for (int i = 0; i < bands_data.bands.size(); ++i) {
    SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<GDALRasterBandH> band,
        api.GDALGetRasterBand(&dataset_ptr, i + 1));
    SAPI_FAIL_IF_NOT(band.value() != nullptr, 
        "Error getting band from dataset");
    sapi::v::RemotePtr band_ptr(band.value());

    sapi::v::Array<int> data_array(bands_data.bands[i].data.data(), 
        bands_data.bands[i].data.size());

    SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<CPLErr> result, 
        api.GDALRasterIO(&band_ptr, GF_Write, 0, 0,
          bands_data.bands[i].width, bands_data.bands[i].height, 
          data_array.PtrBefore(), bands_data.bands[i].width, 
          bands_data.bands[i].height, GDALDataType::GDT_Int32, 0, 0));

    SAPI_FAIL_IF_NOT(result.value() == CPLErr::CE_None, 
        "Error writing band to dataset");

    SAPI_ASSIGN_OR_RETURN(result, api.GDALSetRasterColorInterpretation(
        &band_ptr, static_cast<GDALColorInterp>(
              bands_data.bands[i].color_interp)));

    SAPI_FAIL_IF_NOT(result.value() == CPLErr::CE_None,
        "Error setting color interpretation");

    if (bands_data.bands[i].no_data_value.has_value()) {
      SAPI_ASSIGN_OR_RETURN(result, 
          api.GDALSetRasterNoDataValue(&band_ptr, 
            bands_data.bands[i].no_data_value.value()));

      SAPI_FAIL_IF_NOT(result.value() == CPLErr::CE_None, 
          "Error setting no data value for the band");
    }
  }

  if (bands_data.wkt_projection.length() > 0) {
    sapi::v::ConstCStr wkt_projection_ptr(bands_data.wkt_projection.data());
    SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<CPLErr> result, 
        api.GDALSetProjection(&dataset_ptr, wkt_projection_ptr.PtrBefore()));
    SAPI_FAIL_IF_NOT(result.value() == CPLErr::CE_None,
        "Error setting wkt projection");
  }

  if (bands_data.geo_transform.size() > 0) {
    sapi::v::Array<double> geo_transform_ptr(bands_data.geo_transform.data(), 
        bands_data.geo_transform.size());
    SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<CPLErr> result, 
        api.GDALSetGeoTransform(&dataset_ptr, geo_transform_ptr.PtrBefore()));

    SAPI_FAIL_IF_NOT(result.value() == CPLErr::CE_None, 
        "Error setting geo transform");
  }

  SAPI_RETURN_IF_ERROR(api.GDALClose(&dataset_ptr));

  return absl::OkStatus();
}

void Usage() {
  std::cerr << "Example application that converts raster data to GTiff"
                " format inside the sandbox. Usage:\n"
                "raster_to_gtiff input_filename output_filename\n" 
                "output_filename must be absolute" << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
  using namespace gdal::sandbox;

  if (argc < 3 
      || !std::filesystem::path(std::string(argv[2])).is_absolute()) {
    Usage();
    return EXIT_FAILURE;
  }

  std::string input_data_path = std::string(argv[1]);
  std::string output_data_path = std::string(argv[2]);

  tests::parser::RasterDataset bands_data 
      = tests::parser::GetRasterBandsFromFile(std::move(input_data_path));

  if (absl::Status status = 
      SaveToGTiff(std::move(bands_data), std::move(output_data_path)); 
        !status.ok()) {
    std::cerr << status.ToString() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
