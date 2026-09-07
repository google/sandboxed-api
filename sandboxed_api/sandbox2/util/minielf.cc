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

#include "sandboxed_api/sandbox2/util/minielf.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/util/elf_parser.h"
#include "sandboxed_api/util/fileops.h"

namespace sandbox2 {

absl::StatusOr<ElfFile> ElfFile::ParseFromFile(const std::string& filename,
                                               uint32_t features,
                                               bool mmap_file) {
  // Users may create lots of sandboxes at the same time in address-space
  // restricted environments. So we use the slower non-mmap mode to conserve
  // virtual address space.
  ABSL_ASSIGN_OR_RETURN(auto parser, ElfParser::Create(filename, mmap_file));
  return Parse(*parser, features);
}

absl::StatusOr<ElfFile> ElfFile::ParseFromFd(
    sapi::file_util::fileops::FDCloser fd, uint32_t features, bool mmap_file) {
  // Users may create lots of sandboxes at the same time in address-space
  // restricted environments. So we use the slower non-mmap mode to conserve
  // virtual address space.
  ABSL_ASSIGN_OR_RETURN(auto parser,
                        ElfParser::Create(std::move(fd), mmap_file));
  return Parse(*parser, features);
}

absl::StatusOr<ElfFile> ElfFile::Parse(ElfParser& parser, uint32_t features) {
  // Basic sanity check.
  if (features & ~(ElfFile::kAll)) {
    return absl::InvalidArgumentError("Unknown feature flags specified");
  }
  ElfFile result;
  switch (parser.file_header().e_type) {
    case ET_EXEC:
      result.position_independent_ = false;
      break;
    case ET_DYN:
      result.position_independent_ = true;
      break;
    default:
      return absl::FailedPreconditionError("not an executable: ");
  }
  if (features & ElfFile::kGetInterpreter) {
    ABSL_ASSIGN_OR_RETURN(result.interpreter_, parser.ReadInterpreter());
  }

  if (features & ElfFile::kLoadSymbols) {
    ABSL_RETURN_IF_ERROR(parser.ForEachSection(
        [&](absl::string_view /*name*/, const ElfShdr& hdr) -> absl::Status {
          if (hdr.sh_type == SHT_SYMTAB) {
            ABSL_RETURN_IF_ERROR(parser.ReadSymbolsFromSymtab(
                hdr, [&result](uintptr_t address, absl::string_view name) {
                  result.symbols_.push_back({address, std::string(name)});
                }));
          }
          return absl::OkStatus();
        }));
  }

  if (features & ElfFile::kLoadImportedLibraries) {
    ABSL_ASSIGN_OR_RETURN(result.imported_libraries_,
                          parser.ReadImportedLibraries());
  }

  return std::move(result);
}

absl::StatusOr<ElfSectionLocation> ElfFile::GetSectionLocation(
    int fd, absl::string_view section_name) {
  int dup_fd = dup(fd);
  if (dup_fd < 0) {
    return absl::ErrnoToStatus(errno, "dup failed");
  }
  ABSL_ASSIGN_OR_RETURN(
      auto parser, ElfParser::Create(sapi::file_util::fileops::FDCloser(dup_fd),
                                     /*mmap_file=*/false));
  ElfSectionLocation location;
  bool found = false;
  ABSL_RETURN_IF_ERROR(parser->ForEachSection(
      [&](absl::string_view name, const ElfShdr& hdr) -> absl::Status {
        if (name == section_name) {
          location.offset = hdr.sh_offset;
          location.size = hdr.sh_size;
          found = true;
        }
        return absl::OkStatus();
      }));
  if (!found) {
    return absl::NotFoundError(
        absl::StrCat("ELF section not found: ", section_name));
  }
  return location;
}

absl::StatusOr<ElfSectionLocation> ElfFile::GetSectionLocation(
    const std::string& filename, absl::string_view section_name) {
  int fd = open(filename.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return absl::ErrnoToStatus(
        errno, absl::StrCat("Failed to open ELF file: ", filename));
  }
  sapi::file_util::fileops::FDCloser fd_closer(fd);
  return GetSectionLocation(fd, section_name);
}

}  // namespace sandbox2
