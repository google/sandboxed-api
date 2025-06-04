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

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/util/elf_parser.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {

absl::StatusOr<ElfFile> ElfFile::ParseFromFile(const std::string& filename,
                                               uint32_t features,
                                               bool mmap_file) {
  // Basic sanity check.
  if (features & ~(ElfFile::kAll)) {
    return absl::InvalidArgumentError("Unknown feature flags specified");
  }
  // Users may create lots of sandboxes at the same time in address-space
  // restricted environments. So we use the slower non-mmap mode to conserve
  // virtual address space.
  SAPI_ASSIGN_OR_RETURN(auto parser, ElfParser::Create(filename, mmap_file));
  ElfFile result;
  switch (parser->file_header().e_type) {
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
    SAPI_ASSIGN_OR_RETURN(result.interpreter_, parser->ReadInterpreter());
  }

  if (features & ElfFile::kLoadSymbols) {
    SAPI_RETURN_IF_ERROR(parser->ForEachSection(
        [&](absl::string_view /*name*/, const ElfShdr& hdr) -> auto {
          if (hdr.sh_type == SHT_SYMTAB) {
            SAPI_RETURN_IF_ERROR(parser->ReadSymbolsFromSymtab(
                hdr, [&result](uintptr_t address, absl::string_view name) {
                  result.symbols_.push_back({address, std::string(name)});
                }));
          }
          return absl::OkStatus();
        }));
  }

  if (features & ElfFile::kLoadImportedLibraries) {
    SAPI_ASSIGN_OR_RETURN(result.imported_libraries_,
                          parser->ReadImportedLibraries());
  }

  return std::move(result);
}

}  // namespace sandbox2
