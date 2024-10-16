// Copyright 2019 Google LLC
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

// Checks various things related to namespaces, depending on the first argument:
// ./binary 0 <file1> <file2> ... <fileN>:
//    Make sure all provided files exist and are RO, return 0 on OK.
//    Returns the index of the first non-existing file on failure.
// ./binary 1 <file1> <file2> ... <fileN>:
//    Make sure all provided files exist and are RW, return 0 on OK.
//    Returns the index of the first non-existing file on failure.
// ./binary 2
//    Make sure that we run in a PID namespace (this implies getpid() == 1)
//    Returns 0 on OK.
// ./binary 3 <uid> <gid>
//    Make sure getuid()/getgid() returns the provided uid/gid (User namespace).
//    Returns 0 on OK.
// ./binary 4 <file1> <file2> ... <fileN>:
//    Create provided files, return 0 on OK.
//    Returns the index of the first non-creatable file on failure.
#include <fcntl.h>
#include <ifaddrs.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"

namespace {

using sapi::file::JoinPath;
using sapi::file_util::fileops::ListDirectoryEntries;

bool IsDirectory(const std::string& path) {
  struct stat statbuf;
  PCHECK(lstat(path.c_str(), &statbuf) == 0) << "Failed to stat " << path;
  return statbuf.st_mode & S_IFDIR;
}

void ListDirectoriesRecursively(const std::string& path,
                                std::vector<std::string>& files) {
  std::string error;
  std::vector<std::string> entries;
  CHECK(ListDirectoryEntries(path, &entries, &error)) << error;
  for (const std::string& entry : entries) {
    std::string new_path = JoinPath(path, entry);
    // Don't descent into /sys or /proc, just mark their existence
    if (new_path == "/sys" || new_path == "/proc") {
      files.push_back(new_path);
      continue;
    }
    if (IsDirectory(new_path)) {
      ListDirectoriesRecursively(new_path, files);
    } else {
      files.push_back(new_path);
    }
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    return 0;
  }

  int mode = atoi(argv[1]);  // NOLINT(runtime/deprecated_fn)
  std::vector<std::string> result;

  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);

  switch (mode) {
    case 0:
      // Make sure file exist
      for (int i = 2; i < argc; i++) {
        if (access(argv[i], R_OK) == 0) {
          result.push_back(argv[i]);
        }
      }
      break;

    case 1:
      for (int i = 2; i < argc; i++) {
        if (access(argv[i], W_OK) == 0) {
          result.push_back(argv[i]);
        }
      }
      break;

    case 2:
      result.push_back(absl::StrCat(getpid()));
      break;

    case 3:
      result.push_back(absl::StrCat(getuid()));
      result.push_back(absl::StrCat(getgid()));
      break;

    case 4:
      for (int i = 2; i < argc; ++i) {
        if (open(argv[i], O_CREAT | O_WRONLY, 0644) != -1) {
          result.push_back(argv[i]);
        }
      }
      break;

    case 5: {
      absl::flat_hash_set<std::string> ifnames;
      struct ifaddrs* addrs;
      if (getifaddrs(&addrs)) {
        return -1;
      }
      for (struct ifaddrs* cur = addrs; cur; cur = cur->ifa_next) {
        ifnames.insert(cur->ifa_name);
      }
      result.insert(result.end(), ifnames.begin(), ifnames.end());
      freeifaddrs(addrs);
      break;
    }

    case 6:
      ListDirectoriesRecursively(argv[2], result);
      break;
    case 7: {
      char hostname[1000];
      if (gethostname(hostname, sizeof(hostname)) == -1) {
        return -1;
      }
      result.push_back(hostname);
      break;
    }

    default:
      return 1;
  }

  CHECK(comms.SendUint64(result.size()));
  for (const std::string& entry : result) {
    CHECK(comms.SendString(entry));
  }
  return 0;
}
