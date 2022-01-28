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

#ifndef RASTER_TO_GTIFF_GDAL_SANDBOX_H_
#define RASTER_TO_GTIFF_GDAL_SANDBOX_H_

#include <syscall.h>

#include <string>

#include "gdal_sapi.sapi.h"  // NOLINT(build/include)

namespace gdal::sandbox {

class GdalSapiSandbox : public GdalSandbox {
 public:
  GdalSapiSandbox(std::string out_directory_path, std::string proj_db_path,
                  time_t time_limit = 0)
      : out_directory_path_(std::move(out_directory_path)),
        proj_db_path_(std::move(proj_db_path)) {
    SetWallTimeLimit(time_limit).IgnoreError();
  }

 private:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .AllowDynamicStartup()
        .AllowRead()
        .AllowSystemMalloc()
        .AllowWrite()
        .AllowExit()
        .AllowOpen()
        .AllowSyscalls({
            __NR_futex,
            __NR_getdents64,  // DriverRegisterAll()
            __NR_lseek,       // GDALCreate()
            __NR_getpid,      // GDALCreate()
            __NR_sysinfo,     // VSI_TIFFOpen_common()
            __NR_prlimit64,   // CPLGetUsablePhysicalRAM()
            __NR_ftruncate,   // GTiffDataset::FillEmptyTiles()
            __NR_unlink,      // GDALDriver::Delete()
        })
        .AddFile(proj_db_path_)  // proj.db is required for some projections
        .AddDirectory(out_directory_path_, /*is_ro=*/false)
        .BuildOrDie();
  }

  std::string out_directory_path_;
  std::string proj_db_path_;
};

}  // namespace gdal::sandbox

#endif  // RASTER_TO_GTIFF_GDAL_SANDBOX_H_
