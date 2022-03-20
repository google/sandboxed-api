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
#include "contrib/libpcap/sandboxed.h"
#include "contrib/libpcap/utils/utils_libpcap.h"

int main(int argc, char* argv[]) {
  std::string prog_name(argv[0]);
  google::InitGoogleLogging(argv[0]);
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);

  if (args.size() != 2 && args.size() != 3) {
    std::cerr << "Usage " << prog_name << " [PCAP_FILE] [FILTER]\n";
    return EXIT_FAILURE;
  }

  LibpcapSapiSandbox sandbox;
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }

  LibPcap libpcap(&sandbox, args[1]);
  if (!libpcap.IsInit()) {
    std::cerr << libpcap.GetInitStatus() << "\n";
    return EXIT_FAILURE;
  }

  if (args.size() == 3) {
    absl::Status status = libpcap.SetFilter(args[2]);
    if (!status.ok()) {
      std::cerr << status << "\n";
      return EXIT_FAILURE;
    }
  }

  while (true) {
    absl::StatusOr<LibPcapPacket> pkg = libpcap.Next();
    if (!pkg.ok()) {
      std::cerr << pkg.status() << "\n";
      return EXIT_FAILURE;
    }
    if (pkg->Finished()) {
      break;
    }
    std::cout << *pkg << "\n";
  }

  return EXIT_SUCCESS;
}
