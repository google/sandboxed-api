// Copyright 2020 Google LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sandboxed_api/sandbox2/util/minielf.h"

#include <cstdint>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/sandbox2/testing.h"
#include "sandboxed_api/sandbox2/util/maps_parser.h"
#include "sandboxed_api/util/status_matchers.h"

using sapi::IsOk;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::StrEq;

extern "C" void ExportedFunctionName() {
  // Don't do anything - used to generate a symbol.
}

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
  char maps_buffer[1024 * 1024]{};
  FILE *f = fopen("/proc/self/maps", "r");
  ASSERT_THAT(f, Not(Eq(nullptr)));
  fread(maps_buffer, 1, sizeof(maps_buffer), f);
  fclose(f);

  auto maps_or = ParseProcMaps(maps_buffer);
  ASSERT_THAT(maps_or.status(), IsOk());
  std::vector<MapsEntry> maps = maps_or.ValueOrDie();

  // Find maps entry that covers this entry.
  uint64_t function_address = reinterpret_cast<uint64_t>(ExportedFunctionName);
  bool entry_found = false;
  for (const auto &entry : maps) {
    if (entry.start <= function_address && entry.end > function_address) {
      entry_found = true;
      function_address -= entry.start;
      break;
    }
  }
  ASSERT_THAT(entry_found, IsTrue());

  uint64_t exported_function_name__symbol_value = 0;

  for (const auto &s : elf.symbols()) {
    if (s.name == "ExportedFunctionName") {
      exported_function_name__symbol_value = s.address;
      break;
    }
  }

  EXPECT_THAT(exported_function_name__symbol_value, function_address);
}

TEST(MinielfTest, ImportedLibraries) {
  SAPI_ASSERT_OK_AND_ASSIGN(
      ElfFile elf, ElfFile::ParseFromFile(
                       GetTestSourcePath("sandbox2/util/testdata/hello_world"),
                       ElfFile::kLoadImportedLibraries));
  std::vector<std::string> imported_libraries = {"libc.so.6"};
  EXPECT_THAT(elf.imported_libraries(), Eq(imported_libraries));
}

}  // namespace
}  // namespace sandbox2
