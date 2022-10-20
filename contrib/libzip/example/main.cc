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
#include "contrib/libzip/sandboxed.h"
#include "contrib/libzip/utils/utils_zip.h"

ABSL_FLAG(bool, list, false, "list files");
ABSL_FLAG(std::string, unzip, "", "unzip");
ABSL_FLAG(std::string, add_file, "", "add file");
ABSL_FLAG(std::string, delete, "", "delete");

absl::Status ListFiles(LibZip& zip) {
  SAPI_ASSIGN_OR_RETURN(int64_t num, zip.GetNumberEntries());
  for (uint64_t i = 0; i < num; i++) {
    SAPI_ASSIGN_OR_RETURN(std::string name, zip.GetName(i));
    std::cout << name << "\n";
  }

  return absl::OkStatus();
}

absl::Status UnzipToStdout(LibZip& zip, const std::string& filename) {
  SAPI_ASSIGN_OR_RETURN(std::vector<uint8_t> buf, zip.ReadFile(filename));

  std::cout << buf.data();

  return absl::OkStatus();
}

absl::Status AddFile(LibZip& zip, const std::string& filename) {
  int fd = open(filename.c_str(), O_RDONLY);
  if (fd < 0) {
    return absl::UnavailableError(
        absl::StrCat("Unable to open file ", filename));
  }

  // The fd will be consumed
  SAPI_ASSIGN_OR_RETURN(uint64_t index, zip.AddFile(filename, fd));

  return absl::OkStatus();
}

absl::Status DeleteFile(LibZip& zip, const std::string& filename) {
  int64_t index = -1;

  SAPI_ASSIGN_OR_RETURN(int64_t num, zip.GetNumberEntries());
  for (uint64_t i = 0; i < num; i++) {
    SAPI_ASSIGN_OR_RETURN(std::string name, zip.GetName(i));
    if (name == filename) {
      index = i;
      break;
    }
  }
  if (index == -1) {
    return absl::UnavailableError(
        absl::StrCat("Unable to remove file ", filename));
  }

  return zip.DeleteFile(index);
}

int main(int argc, char* argv[]) {
  std::string prog_name(argv[0]);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  if (args.size() < 2 || args.size() > 3) {
    std::cerr << "Usage:\n  " << prog_name << " ZIPFILE [OUTFILE]\n";
    return EXIT_FAILURE;
  }

  std::string filename(args[1]);
  ZipSapiSandbox sandbox;
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }

  LibZip zip(&sandbox, filename, 0);

  if (!zip.IsOpen()) {
    std::cerr << "Unable to open file " << filename << "\n";
    return EXIT_FAILURE;
  }

  int outfd = -1;
  if (args.size() == 3) {
    outfd = open(args[2], O_WRONLY | O_CREAT);
    if (outfd < 0) {
      std::cerr << "Unable to open file " << args[2] << "\n";
      return EXIT_FAILURE;
    }
  }

  bool needs_saving = false;
  absl::Status status;
  if (absl::GetFlag(FLAGS_list)) {
    status = ListFiles(zip);
  }
  if (!absl::GetFlag(FLAGS_unzip).empty()) {
    status = UnzipToStdout(zip, absl::GetFlag(FLAGS_unzip));
  }
  if (!absl::GetFlag(FLAGS_add_file).empty()) {
    status = AddFile(zip, absl::GetFlag(FLAGS_add_file));
    needs_saving = true;
  }
  if (!absl::GetFlag(FLAGS_delete).empty()) {
    status = DeleteFile(zip, absl::GetFlag(FLAGS_delete));
    needs_saving = true;
  }

  if (!status.ok()) {
    std::cerr << status << "\n";
    return EXIT_FAILURE;
  }

  status = zip.Finish();
  if (!status.ok()) {
    std::cerr << status << "\n";
    return EXIT_FAILURE;
  }

  if (needs_saving) {
    if (outfd == -1) {
      status = zip.Save();
    } else {
      status = zip.Save(outfd);
    }
    if (!status.ok()) {
      std::cerr << status << "\n";
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}
