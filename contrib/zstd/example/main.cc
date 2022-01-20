// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Google LLC
//                Mariusz Zaborski <oshogbo@invisiblethingslab.com>

#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>

#include "../sandboxed.h"
#include "../utils/utils_zstd.h"

void usage(const std::string& name) {
  std::cerr << "Usage:\n";
  std::cerr << "    " << name << " [-dm] [-l level] [filein] [fileout]\n\n";
  std::cerr << "      -d decompress\n";
  std::cerr << "      -m in memory operations\n";
  std::cerr << "      -l compression level\n";
}

int main(int argc, char** argv) {
  bool memMode = false;
  bool deCompressMode = false;
  int compressLevel = 0;
  char opt;
  std::string progName(argv[0]);
  google::InitGoogleLogging(argv[0]);

  while ((opt = getopt(argc, argv, "mdl:")) != -1) {
    switch (opt) {
      case 'm':
        memMode = true;
        break;
      case 'd':
        deCompressMode = true;
        break;
      case 'l':
        compressLevel = std::stoi(optarg);
        break;
      default:
        usage(progName);
        return 0;
    }
  }
  argc -= optind;
  argv += optind;

  if (argc != 2) {
    usage(progName);
    return 0;
  }

  std::ifstream infile(argv[0], std::ios::binary);
  if (!infile.is_open()) {
    std::cerr << "Unable to open " << argv[0] << std::endl;
    return 1;
  }
  std::ofstream outfile(argv[1], std::ios::binary);
  if (!outfile.is_open()) {
    std::cerr << "Unable to open " << argv[1] << std::endl;
    return 1;
  }

  ZstdSapiSandbox sandbox;
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return 1;
  }

  ZstdApi api(&sandbox);

  absl::Status status;
  if (memMode && deCompressMode) {
    status = DecompressInMemory(api, infile, outfile);
  } else if (memMode && !deCompressMode) {
    status = CompressInMemory(api, infile, outfile, compressLevel);
  } else if (!memMode && deCompressMode) {
    status = DecompressStream(api, infile, outfile);
  } else {
    status = CompressStream(api, infile, outfile, compressLevel);
  }

  if (!status.ok()) {
    std::cerr << "Unable to ";
    std::cerr << (deCompressMode ? "decompress" : "compress");
    std::cerr << " file.\n" << status << "\n";
    return 1;
  }

  return 0;
}
