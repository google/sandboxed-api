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

#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "contrib/libxls/sandboxed.h"
#include "contrib/libxls/utils/utils_libxls.h"

ABSL_FLAG(uint32_t, sheet, 0, "sheet number");

int main(int argc, char* argv[]) {
  std::string prog_name(argv[0]);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  if (args.size() != 2) {
    std::cerr << "Usage:\n  " << prog_name << " INPUT\n";
    return EXIT_FAILURE;
  }

  LibxlsSapiSandbox sandbox(args[1]);
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }

  absl::StatusOr<LibXlsWorkbook> wb = LibXlsWorkbook::Open(&sandbox, args[1]);

  uint32_t nr_sheet = absl::GetFlag(FLAGS_sheet);

  absl::StatusOr<LibXlsSheet> sheet = wb->OpenSheet(nr_sheet);
  if (!sheet.ok()) {
    std::cerr << "Unable to switch sheet: ";
    std::cerr << sheet.status() << "\n";
    return EXIT_FAILURE;
  }

  for (size_t row = 0; row < sheet->GetRowCount(); ++row) {
    for (size_t col = 0; col < sheet->GetColCount(); ++col) {
      absl::StatusOr<LibXlsCell> cell = sheet->GetCell(row, col);
      if (!cell.ok()) {
        std::cerr << "Unable to get cell: ";
        std::cerr << cell.status() << "\n";
        return EXIT_FAILURE;
      }
      switch (cell->type) {
        case XLS_RECORD_NUMBER:
          std::cout << std::setw(16) << std::get<double>(cell->value) << " | ";
          break;
        case XLS_RECORD_STRING:
          std::cout << std::setw(16) << std::get<std::string>(cell->value)
                    << " | ";
          break;
        case XLS_RECORD_BOOL:
          std::cout << std::setw(16) << std::get<bool>(cell->value) << " | ";
          break;
        case XLS_RECORD_BLANK:
          std::cout << std::setw(16) << " | ";
          break;
        case XLS_RECORD_ERROR:
          std::cout << "error"
                    << "\n";
          break;
      }
    }
    std::cout << "\n";
  }
  return EXIT_SUCCESS;
}
