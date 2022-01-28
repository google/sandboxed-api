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

#ifndef RASTER_TO_GTIFF_GTIFF_CONVERTER_H_
#define RASTER_TO_GTIFF_GTIFF_CONVERTER_H_

#include <string>

#include "gdal_sandbox.h"     // NOLINT(build/include)
#include "get_raster_data.h"  // NOLINT(build/include)
#include "sandboxed_api/transaction.h"

namespace gdal::sandbox {

class RasterToGTiffProcessor : public sapi::Transaction {
 public:
  RasterToGTiffProcessor(std::string out_file_full_path,
                         std::string proj_db_path, parser::RasterDataset data,
                         int retry_count = 0);

 private:
  absl::Status Main() final;

  const std::string out_file_full_path_;
  parser::RasterDataset data_;
};

}  // namespace gdal::sandbox

#endif  // RASTER_TO_GTIFF_GTIFF_CONVERTER_H_
