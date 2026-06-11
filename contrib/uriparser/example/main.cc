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

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "contrib/uriparser/sandboxed.h"
#include "contrib/uriparser/utils/utils_uriparser.h"

void Print(const char* name, const absl::StatusOr<std::string>& r) {
  if (!r.ok()) {
    std::cerr << "Unable to fetch " << name << "\n";
    std::cerr << r.status() << "\n";
    return;
  }

  if (r.value().empty()) {
    return;
  }

  std::cout << name << ": " << *r << "\n";
}

int main(int argc, char* argv[]) {
  std::string prog_name(argv[0]);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  std::vector<char*> args = absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  if (args.size() < 2) {
    std::cerr << "Usage:\n  " << prog_name << " URI ...\n";
    return EXIT_FAILURE;
  }

  UriparserSapiSandbox sandbox;
  if (!sandbox.Init().ok()) {
    std::cerr << "Unable to start sandbox\n";
    return EXIT_FAILURE;
  }

  int retval = EXIT_SUCCESS;
  for (int i = 1; i < args.size(); ++i) {
    UriParser uri(&sandbox, args[i]);
    if (!uri.GetStatus().ok()) {
      std::cerr << "Unable to parse: " << args[i] << "\n";
      std::cerr << uri.GetStatus() << "\n";
      retval = EXIT_FAILURE;
      continue;
    }

    Print("scheme", uri.GetScheme());
    Print("user info", uri.GetUserInfo());
    Print("host", uri.GetHostText());
    Print("host IP", uri.GetHostIP());
    Print("port", uri.GetPortText());
    Print("query", uri.GetQuery());
    Print("fragment", uri.GetFragment());

    absl::StatusOr<std::vector<std::string>> path = uri.GetPath();
    if (!path.ok()) {
      std::cerr << "Unable to get path.\n";
      std::cerr << path.status() << "\n";
      retval = EXIT_FAILURE;
      continue;
    }
    if (!path->empty()) {
      std::cout << "pathSeq: \n";
      for (const auto& s : path.value()) {
        std::cout << " - " << s << "\n";
      }
    }

    absl::StatusOr<absl::btree_map<std::string, std::string>> query_map;
    query_map = uri.GetQueryElements();
    if (!query_map.ok()) {
      std::cerr << "Unable to get query.\n";
      std::cerr << query_map.status() << "\n";
      retval = EXIT_FAILURE;
      continue;
    }
    if (!query_map->empty()) {
      std::cout << "Query elements: \n";
      for (const auto& mp : query_map.value()) {
        std::cout << " - " << mp.first << ": " << mp.second << "\n";
      }
    }

    if (!uri.NormalizeSyntax().ok()) {
      std::cerr << "Unable to normalize: " << args[i] << "\n";
      continue;
    }
    absl::StatusOr<std::string> newuris = uri.GetUri();
    if (!newuris.ok()) {
      std::cerr << "Unable to reconstruct path.\n";
      std::cerr << newuris.status() << "\n";
      retval = EXIT_FAILURE;
      continue;
    }
    std::cout << "Normalized path: " << newuris.value() << "\n";
  }

  return retval;
}
