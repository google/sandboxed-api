// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may !use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/status/statusor.h"
#include "contrib/libraw/sandboxed.h"
#include "contrib/libraw/utils/utils_libraw.h"

void PrintUsage(const char* name) {
  std::cout << "Dump (small) selecton of RAW file as tab-separated text file\n"
            << "Usage: " << name
            << " inputfile COL ROW [CHANNEL] [width] [height]\n"
               "  COL - start column\n"
               "  ROW - start row\n"
               "  CHANNEL - raw channel to dump, default is 0 (red for rggb)\n"
               "  width - area width to dump, default is 16\n"
               "  height - area height to dump, default is 4\n";
}

uint16_t SubtractBlack(uint16_t val, unsigned int bl) {
  return val > bl ? val - bl : 0;
}

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  if (argc < 4) {
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  int colstart = atoi(argv[2]);  // NOLINT(runtime/deprecated_fn)
  int rowstart = atoi(argv[3]);  // NOLINT(runtime/deprecated_fn)
  int channel = 0;
  if (argc > 4) {
    channel = atoi(argv[4]);  // NOLINT(runtime/deprecated_fn)
  }
  int width = 16;
  if (argc > 5) {
    width = atoi(argv[5]);  // NOLINT(runtime/deprecated_fn)
  }
  int height = 4;
  if (argc > 6) {
    height = atoi(argv[6]);  // NOLINT(runtime/deprecated_fn)
  }
  if (width < 1 || height < 1) {
    PrintUsage(argv[0]);
    return EXIT_FAILURE;
  }

  sapi::v::ConstCStr file_name(argv[1]);
  absl::Status status;
  LibRawSapiSandbox sandbox(file_name.GetData());

  status = sandbox.Init();
  if (!status.ok()) {
    std::cerr << "Unable to start sandbox: " << status.message() << "\n";
    return EXIT_FAILURE;
  }

  LibRaw lr(&sandbox, argv[1]);
  if (!lr.CheckIsInit().ok()) {
    std::cerr << "Unable init LibRaw: " << lr.CheckIsInit().message();
    return EXIT_FAILURE;
  }

  status = lr.OpenFile();
  if (!status.ok()) {
    std::cerr << "Unable to open file " << argv[1] << ": " << status.message();
    return EXIT_FAILURE;
  }

  if ((lr.GetColorCount() == 1 && channel > 0) || (channel > 3)) {
    std::cerr << "Incorrect CHANNEL specified: " << channel << "\n";
    return EXIT_FAILURE;
  }

  status = lr.Unpack();
  if (!status.ok()) {
    std::cerr << "Unable to unpack file " << argv[1] << status.message();
    return EXIT_FAILURE;
  }

  status = lr.SubtractBlack();
  if (!status.ok()) {
    std::cerr << "Unable to subtract black level: " << status.message();
    // ok, but different output
  }

  absl::StatusOr<std::vector<uint16_t>> rawdata = lr.RawData();
  if (!rawdata.ok()) {
    std::cerr << "Unable to get raw data: " << rawdata.status().message();
    return EXIT_FAILURE;
  }

  absl::StatusOr<ushort> raw_height = lr.GetRawHeight();
  absl::StatusOr<ushort> raw_width = lr.GetRawWidth();
  if (!raw_height.ok() || !raw_width.ok()) {
    std::cerr << "Unable to get raw image sizes";
    return EXIT_FAILURE;
  }

  // header
  std::cout << argv[1] << "\t" << colstart << "-" << rowstart << "-" << width
            << "x" << height << "\t"
            << "channel: " << channel << "\n";
  std::cout << std::setw(6) << "R\\C";
  for (int col = colstart; col < colstart + width && col < *raw_width; col++) {
    std::cout << std::setw(6) << col;
  }
  std::cout << "\n";

  // dump raw to output
  for (int row = rowstart; row < rowstart + height && row < *raw_height;
       ++row) {
    int rcolors[48];
    if (lr.GetColorCount() > 1) {
      absl::StatusOr<int> color;
      for (int c = 0; c < 48; c++) {
        color = lr.COLOR(row, c);
        if (color.ok()) rcolors[c] = *color;
      }
    } else {
      memset(rcolors, 0, sizeof(rcolors));
    }
    std::cout << std::setw(6) << row;

    for (int col = colstart; col < colstart + width && col < *raw_width;
         ++col) {
      int idx = row * lr.GetImgData().sizes.raw_pitch / 2 + col;

      if (rcolors[col % 48] == channel) {
        absl::StatusOr<unsigned int> cblack = lr.GetCBlack(channel);
        if (!cblack.ok()) {
          std::cerr << "Unable to get cblack for channel " << channel << ": "
                    << cblack.status().message();
          return EXIT_FAILURE;
        }
        std::cout << std::setw(6) << SubtractBlack((*rawdata).at(idx), *cblack);
      } else {
        std::cout << "     -";
      }
    }
    std::cout << "\n";
  }

  return EXIT_SUCCESS;
}
