// Copyright 2022 Google LLC
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

#include <string.h>

#include <iomanip>
#include <iostream>

#include "contrib/libraw/sandboxed.h"
#include "contrib/libraw/utils/utils_libraw.h"

void usage(const char* av) {
  std::cout << "Dump (small) selecton of RAW file as tab-separated text file\n"
            << "Usage: " << av
            << " inputfile COL ROW [CHANNEL] [width] [height]\n"
               "  COL - start column\n"
               "  ROW - start row\n"
               "  CHANNEL - raw channel to dump, default is 0 (red for rggb)\n"
               "  width - area width to dump, default is 16\n"
               "  height - area height to dump, default is 4\n";
}

unsigned subtract_bl(int val, int bl) { return val > bl ? val - bl : 0; }

int main(int ac, char* av[]) {
  google::InitGoogleLogging(av[0]);

  if (ac < 4) {
    usage(av[0]);
    exit(1);
  }
  int colstart = atoi(av[2]);
  int rowstart = atoi(av[3]);
  int channel = 0;
  if (ac > 4) channel = atoi(av[4]);
  int width = 16;
  if (ac > 5) width = atoi(av[5]);
  int height = 4;
  if (ac > 6) height = atoi(av[6]);
  if (width < 1 || height < 1) {
    usage(av[0]);
    exit(1);
  }

  sapi::v::ConstCStr file_name(av[1]);

  LibRawSapiSandbox sandbox(file_name.GetData());
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }

  absl::Status status;

  LibRaw lr(&sandbox, av[1]);
  if (not lr.CheckIsInit().ok()) {
    std::cerr << "Unable init LibRaw";
    std::cerr << lr.CheckIsInit().status();
    return EXIT_FAILURE;
  }

  status = lr.OpenFile();
  if (not status.ok()) {
    std::cerr << "Unable to open file" << av[1] << "\n";
    std::cerr << status;
    return EXIT_FAILURE;
  }

  if ((lr.GetColorCount() == 1 and channel > 0) or (channel > 3)) {
    std::cerr << "Incorrect CHANNEL specified:" << channel << "\n";
    return EXIT_FAILURE;
  }

  status = lr.Unpack();
  if (not status.ok()) {
    std::cerr << "Unable to unpack file" << av[1] << "\n";
    std::cerr << status;
    return EXIT_FAILURE;
  }

  status = lr.SubtractBlack();
  if (not status.ok()) {
    std::cerr << "Unable to subtract black level";
    std::cerr << status;
    // ok, but different output
  }

  absl::StatusOr<std::vector<uint16_t>> rawdata = lr.RawData();
  if (not rawdata.ok()) {
    std::cerr << "Unable to get raw data\n";
    std::cerr << rawdata.status();
    return EXIT_FAILURE;
  }

  absl::StatusOr<ushort> raw_height = lr.GetRawHeight();
  absl::StatusOr<ushort> raw_width = lr.GetRawWidth();
  if (not raw_height.ok() or not raw_width.ok()) {
    std::cerr << "Unable to get raw image sizes.";
    return EXIT_FAILURE;
  }

  // header
  std::cout << av[1] << "\t" << colstart << "-" << rowstart << "-" << width
            << "x" << height << "\t"
            << "channel: " << channel << "\n";
  std::cout << std::setw(6) << "R\\C";
  for (int col = colstart;
       col < colstart + width and col < *raw_width;
       col++) {
    std::cout << std::setw(6) << col;
  }
  std::cout << "\n";

  // dump raw to output
  for (int row = rowstart;
       row < rowstart + height && row < *raw_height;
       row++) {
    unsigned rcolors[48];
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

    for (int col = colstart;
         col < colstart + width && col < *raw_width;
         col++) {
      int idx = row * lr.GetImgData().sizes.raw_pitch / 2 + col;

      if (rcolors[col % 48] == (unsigned)channel) {
        absl::StatusOr<int> cblack = lr.GetCBlack(channel);
        if (not cblack.ok()) {
          std::cerr << "Unable to get cblack for channel " << channel;
          std::cerr << cblack.status();
          return EXIT_FAILURE;
        }
        std::cout << std::setw(6)
                  << subtract_bl((*rawdata).at(idx), *cblack);
      } else {
        std::cout << "     -";
      }
    }
    std::cout << "\n";
  }

  return EXIT_SUCCESS;
}