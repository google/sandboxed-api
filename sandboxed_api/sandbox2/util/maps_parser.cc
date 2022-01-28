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

#include "sandboxed_api/sandbox2/util/maps_parser.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"

namespace sandbox2 {

absl::StatusOr<std::vector<MapsEntry>> ParseProcMaps(
    const std::string& contents) {
  // Note: The format string
  //       https://github.com/torvalds/linux/blob/v4.14/fs/proc/task_mmu.c#L289
  //       changed to a non-format string implementation
  //       (show_vma_header_prefix()).
  static constexpr char kFormatString[] =
      "%lx-%lx %c%c%c%c %lx %x:%x %lu %1023s";
  static constexpr size_t kFilepathLength = 1023;

  std::vector<std::string> lines =
      absl::StrSplit(contents, '\n', absl::SkipEmpty());
  std::vector<MapsEntry> entries;
  for (const auto& line : lines) {
    MapsEntry entry{};
    char r, w, x, s;
    entry.path.resize(kFilepathLength + 1, '\0');
    int n_matches = sscanf(
        line.c_str(), kFormatString, &entry.start, &entry.end, &r, &w, &x, &s,
        &entry.pgoff, &entry.major, &entry.minor, &entry.inode, &entry.path[0]);

    // Some lines do not have a filename.
    if (n_matches == 10) {
      entry.path.clear();
    } else if (n_matches == 11) {
      entry.path.resize(strlen(entry.path.c_str()));
    } else {
      return absl::FailedPreconditionError("Invalid format");
    }
    entry.is_readable = r == 'r';
    entry.is_writable = w == 'w';
    entry.is_executable = x == 'x';
    entry.is_shared = s == 's';
    entries.push_back(entry);
  }
  return entries;
}

}  // namespace sandbox2
