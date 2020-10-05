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
#include <optional>
#include <filesystem>
#include <string>

#include "gtest/gtest.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/sandbox2/util/temp_file.h"
#include "sandboxed_api/sandbox2/util/fileops.h"

#include "gdal_sandbox.h"
#include "get_raster_data.h"

namespace gdal::sandbox::tests {
namespace {

inline constexpr absl::string_view kDriverName = "GTiff";
inline constexpr absl::string_view kTempFilePrefix = "temp_data";
inline constexpr absl::string_view kProjDbEnvVariableName = "PROJ_PATH";
inline constexpr absl::string_view kDefaultProjDbPath 
    = "/usr/local/share/proj/proj.db";
inline constexpr absl::string_view kFirstTestDataPath = 
    "../testdata/map.tif";
inline constexpr absl::string_view kSecondTestDataPath = 
    "../testdata/map_large.tif";
inline constexpr absl::string_view kThirdTestDataPath = 
    "../testdata/earth_map.tif";

std::optional<std::string> GetProjDbPath() {
  const char* proj_db_path_ptr = std::getenv(kProjDbEnvVariableName.data());

  std::string proj_db_path = proj_db_path_ptr == nullptr ? 
      std::string(kDefaultProjDbPath) : std::string(proj_db_path_ptr);

  if (!std::filesystem::exists(proj_db_path)) {
    return std::nullopt;
  }

  return proj_db_path;
}

// RAII wrapper that creates temporary file and automatically unlinks it
class TempFile {
 public:
  explicit TempFile(absl::string_view prefix)
  {
    auto file_data = sandbox2::CreateNamedTempFile(prefix);

    if (file_data.ok()) {
      file_data_ = std::move(file_data.value());
    }
  }

  ~TempFile() {
    if (file_data_.has_value()) {
      unlink(file_data_.value().first.c_str());
    }
  }

  bool HasValue() const {
    return file_data_.has_value();
  }

  int GetFd() const {
    return file_data_.value().second;
  }

  std::string GetPath() const {
    return file_data_.value().first;
  }

 private:
  std::optional<std::pair<std::string, int>> file_data_ = std::nullopt;
};

// Wrapper around raster to GTiff workflow
class RasterToGTiffProcessor : public sapi::Transaction {
 public:
  explicit RasterToGTiffProcessor(std::string out_filename, 
                                  std::string out_path,
                                  std::string proj_db_path,
                                  parser::RasterDataset data,
                                  int retry_count = 0) 
    : sapi::Transaction(std::make_unique<GdalSapiSandbox>(out_path, 
        std::move(proj_db_path))),
      out_filename_(std::move(out_filename)),
      out_path_(std::move(out_path)),
      data_(std::move(data))
  {
    set_retry_count(retry_count);
    SetTimeLimit(absl::InfiniteDuration());
  }

 private:
  absl::Status Main() final;

  const std::string out_filename_;
  const std::string out_path_;
  parser::RasterDataset data_;
};

absl::Status RasterToGTiffProcessor::Main() {
  gdalApi api(sandbox());
  SAPI_RETURN_IF_ERROR(api.GDALAllRegister());

  sapi::v::ConstCStr driver_name_ptr(kDriverName.data());

  SAPI_ASSIGN_OR_RETURN(sapi::StatusOr<GDALDriverH> driver, 
      api.GDALGetDriverByName(driver_name_ptr.PtrBefore()));

  TRANSACTION_FAIL_IF_NOT(driver.value() != nullptr, 
      "Error getting GTiff driver");
  sapi::v::RemotePtr driver_ptr(driver.value());

  std::string out_file_full_path = absl::StrCat(out_path_, "/", out_filename_);
  sapi::v::ConstCStr out_file_full_path_ptr(out_file_full_path.data());
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
    sapi::v::ConstCStr wkt_projection_ptr(data_.wkt_projection.data());
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

}  // namespace

class TestGTiffProcessor : public testing::TestWithParam<absl::string_view> {
 public:
  TestGTiffProcessor() 
    : tempfile_(kTempFilePrefix) 
  {}

 protected:
  const TempFile tempfile_;
};

TEST_P(TestGTiffProcessor, TestProcessorOnGTiffData) {
  std::string filename = std::string(GetParam());

  ASSERT_TRUE(tempfile_.HasValue()) 
      << "Error creating temporary output file";

  parser::RasterDataset original_bands_data = 
      parser::GetRasterBandsFromFile(filename);

  std::optional<std::string> proj_db_path = GetProjDbPath();
  ASSERT_TRUE(proj_db_path != std::nullopt)
      << "Specified proj.db does not exist";

  RasterToGTiffProcessor processor(tempfile_.GetPath(), 
      sandbox2::file_util::fileops::GetCWD(), std::move(proj_db_path.value()),
      original_bands_data);
  
  ASSERT_EQ(processor.Run(), absl::OkStatus())
      << "Error creating new GTiff dataset inside sandbox";
  
  ASSERT_EQ(original_bands_data, 
      parser::GetRasterBandsFromFile(tempfile_.GetPath()))
      << "New dataset doesn't match the original one";
}

INSTANTIATE_TEST_CASE_P(
        GDALTests,
        TestGTiffProcessor,
        ::testing::Values(kFirstTestDataPath, 
                          kSecondTestDataPath,
                          kThirdTestDataPath)
);

}  // namespace gdal::sandbox::tests
