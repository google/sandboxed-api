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

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "get_raster_data.h"  // NOLINT(build/include)
#include "gtiff_converter.h"  // NOLINT(build/include)
#include "utils.h"  // NOLINT(build/include)

namespace {

absl::Status SaveToGTiff(gdal::sandbox::parser::RasterDataset bands_data, 
    std::string out_file) {
  using namespace gdal::sandbox;

  std::string output_data_folder = "";

  if (!sandbox2::file_util::fileops::RemoveLastPathComponent(out_file,
      &output_data_folder)) {
    return absl::FailedPreconditionError("Error getting output file directory");
  }

  std::optional<std::string> proj_db_path = utils::FindProjDbPath();

  if (proj_db_path == std::nullopt) {
    return absl::FailedPreconditionError("Specified proj.db does not exist");
  }

  RasterToGTiffProcessor processor(out_file, output_data_folder, 
                                    proj_db_path.value(), bands_data);

  return processor.Run();
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

  if (argc < 3 || !gdal::sandbox::utils::IsAbsolute(argv[2])) {
    Usage();
    return EXIT_FAILURE;
  }

  std::string input_data_path = std::string(argv[1]);
  std::string output_data_path = std::string(argv[2]);

  if (absl::Status status = 
      SaveToGTiff(parser::GetRasterBandsFromFile(std::move(input_data_path)), 
        std::move(output_data_path)); 
      !status.ok()) {
    std::cerr << status.ToString() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
