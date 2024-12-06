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
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/algorithm/container.h"
#include "sandboxed_api/sandbox2/util/maps_parser.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/file_helpers.h"
#include "sandboxed_api/util/status_matchers.h"

extern "C" void ExportedFunction() {
  // Don't do anything - used to generate a symbol.
}

namespace file = ::sapi::file;
using ::sapi::GetTestSourcePath;
using ::sapi::IsOk;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::StrEq;

namespace sandbox2 {
namespace {

TEST(MinielfTest, Chrome70) {
  SAPI_ASSERT_OK_AND_ASSIGN(
      ElfFile elf,
      ElfFile::ParseFromFile(
          GetTestSourcePath("sandbox2/util/testdata/chrome_grte_header"),
          ElfFile::kGetInterpreter));
  EXPECT_THAT(elf.interpreter(), StrEq("/usr/grte/v4/ld64"));
}

TEST(MinielfTest, SymbolResolutionWorks) {
  SAPI_ASSERT_OK_AND_ASSIGN(
      ElfFile elf,
      ElfFile::ParseFromFile("/proc/self/exe", ElfFile::kLoadSymbols));
  ASSERT_THAT(elf.position_independent(), IsTrue());

  // Load /proc/self/maps to take ASLR into account.
  std::string maps_buffer;
  ASSERT_THAT(
      file::GetContents("/proc/self/maps", &maps_buffer, file::Defaults()),
      IsOk());
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<MapsEntry> maps,
                            ParseProcMaps(maps_buffer));

  // Find maps entry that covers this entry.
  uint64_t function_address = reinterpret_cast<uint64_t>(&ExportedFunction);
  auto entry =
      absl::c_find_if(maps, [function_address](const MapsEntry& entry) {
        return entry.start <= function_address && entry.end > function_address;
      });
  ASSERT_THAT(entry, Ne(maps.end()));

  auto function_symbol =
      absl::c_find_if(elf.symbols(), [](const ElfFile::Symbol& symbol) {
        return symbol.name == "ExportedFunction";
      });
  ASSERT_THAT(function_symbol, Ne(elf.symbols().end()));

  function_address -= entry->start - entry->pgoff;
  EXPECT_THAT(function_symbol->address, Eq(function_address));
}

TEST(MinielfTest, ImportedLibraries) {
  SAPI_ASSERT_OK_AND_ASSIGN(
      ElfFile elf, ElfFile::ParseFromFile(
                       GetTestSourcePath("sandbox2/util/testdata/hello_world"),
                       ElfFile::kLoadImportedLibraries));
  EXPECT_THAT(elf.imported_libraries(), ElementsAre("libc.so.6"));
}

}  // namespace
}  // namespace sandbox2
