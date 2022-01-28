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

#include <optional>
#include <string>

#include "gdal_sandbox.h"     // NOLINT(build/include)
#include "get_raster_data.h"  // NOLINT(build/include)
#include "gtiff_converter.h"  // NOLINT(build/include)
#include "gtest/gtest.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "utils.h"  // NOLINT(build/include)

namespace {

inline constexpr absl::string_view kTempFilePrefix = "temp_data";
inline constexpr absl::string_view kFirstTestDataPath = "testdata/cea.tif";
inline constexpr absl::string_view kSecondTestDataPath =
    "testdata/SP27GTIF.tif";

}  // namespace

class TestGTiffProcessor : public testing::TestWithParam<absl::string_view> {
 public:
  TestGTiffProcessor() : tempfile_(sandbox2::GetTestTempPath()) {}

 protected:
  const gdal::sandbox::utils::TempFile tempfile_;
};

TEST_P(TestGTiffProcessor, TestProcessorOnGTiffData) {
  std::string file_path = gdal::sandbox::utils::GetTestDataPath(GetParam());

  ASSERT_TRUE(sandbox2::file_util::fileops::Exists(file_path, false))
      << "Error finding input dataset";

  ASSERT_TRUE(tempfile_.HasValue()) << "Error creating temporary output file";

  gdal::sandbox::parser::RasterDataset original_bands_data =
      gdal::sandbox::parser::GetRasterBandsFromFile(file_path);

  std::optional<std::string> proj_db_path =
      gdal::sandbox::utils::FindProjDbPath();
  ASSERT_TRUE(proj_db_path != std::nullopt)
      << "Specified proj.db does not exist";

  gdal::sandbox::RasterToGTiffProcessor processor(
      tempfile_.GetPath(), std::move(proj_db_path.value()),
      original_bands_data);

  ASSERT_EQ(processor.Run(), absl::OkStatus())
      << "Error creating new GTiff dataset inside sandbox";

  ASSERT_EQ(original_bands_data,
            gdal::sandbox::parser::GetRasterBandsFromFile(tempfile_.GetPath()))
      << "New dataset doesn't match the original one";
}

INSTANTIATE_TEST_CASE_P(GDALTests, TestGTiffProcessor,
                        ::testing::Values(kFirstTestDataPath,
                                          kSecondTestDataPath));
