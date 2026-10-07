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

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "sandboxed_api/sandbox2/util/maps_parser.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/util/file_helpers.h"
#include "sandboxed_api/util/fileops.h"

extern "C" void ExportedFunction() {
  // Don't do anything - used to generate a symbol.
}

namespace file = ::sapi::file;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::sapi::GetTestSourcePath;
using ::sapi::file_util::fileops::FDCloser;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::Not;
using ::testing::StrEq;

namespace sandbox2 {
namespace {

class MinielfTest : public testing::TestWithParam<bool> {
 protected:
  bool mmap_file() const { return GetParam(); }
};

TEST_P(MinielfTest, Chrome70) {
  SAPI_ASSERT_OK_AND_ASSIGN(
      ElfFile elf,
      ElfFile::ParseFromFile(
          GetTestSourcePath("sandbox2/util/testdata/chrome_grte_header"),
          ElfFile::kGetInterpreter, mmap_file()));
  EXPECT_THAT(elf.interpreter(), StrEq("/usr/grte/v4/ld64"));
}

TEST_P(MinielfTest, FromFD) {
  FDCloser fd(open(
      GetTestSourcePath("sandbox2/util/testdata/chrome_grte_header").c_str(),
      O_RDONLY));
  SAPI_ASSERT_OK_AND_ASSIGN(
      ElfFile elf, ElfFile::ParseFromFd(std::move(fd), ElfFile::kGetInterpreter,
                                        mmap_file()));
  EXPECT_THAT(elf.interpreter(), StrEq("/usr/grte/v4/ld64"));
}

TEST_P(MinielfTest, InvalidFdFails) {
  EXPECT_THAT(
      ElfFile::ParseFromFd(FDCloser(), ElfFile::kGetInterpreter, mmap_file()),
      Not(IsOk()));
}

TEST_P(MinielfTest, SymbolResolutionWorks) {
  SAPI_ASSERT_OK_AND_ASSIGN(
      ElfFile elf, ElfFile::ParseFromFile("/proc/self/exe",
                                          ElfFile::kLoadSymbols, mmap_file()));
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

TEST_P(MinielfTest, ImportedLibraries) {
  SAPI_ASSERT_OK_AND_ASSIGN(
      ElfFile elf, ElfFile::ParseFromFile(
                       GetTestSourcePath("sandbox2/util/testdata/hello_world"),
                       ElfFile::kLoadImportedLibraries, mmap_file()));
  EXPECT_THAT(elf.imported_libraries(), ElementsAre("libc.so.6"));
}

INSTANTIATE_TEST_SUITE_P(Suite, MinielfTest, testing::Values(false, true));

TEST(MinielfTest, GetSectionLocationWorks) {
  const std::string path =
      GetTestSourcePath("sandbox2/util/testdata/hello_world");
  SAPI_ASSERT_OK_AND_ASSIGN(ElfSectionLocation location,
                            ElfFile::GetSectionLocation(path, ".interp"));
  EXPECT_THAT(location.offset, Eq(0x238));
  EXPECT_THAT(location.size, Eq(0x1c));

  FDCloser fd(open(path.c_str(), O_RDONLY));
  ASSERT_THAT(fd.get(), Ne(-1));
  std::string contents(location.size - 1, '\0');
  ASSERT_THAT(
      pread(fd.get(), contents.data(), contents.size(), location.offset),
      Eq(contents.size()));
  EXPECT_THAT(contents, StrEq("/lib64/ld-linux-x86-64.so.2"));
}

TEST(MinielfTest, GetSectionLocationFailsForNonExistentSection) {
  EXPECT_THAT(ElfFile::GetSectionLocation(
                  GetTestSourcePath("sandbox2/util/testdata/hello_world"),
                  ".nonexistent_section"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(MinielfTest, GetSectionLocationFailsForNonExistentFile) {
  EXPECT_THAT(ElfFile::GetSectionLocation("/nonexistent/path/to/elf", ".text"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(MinielfTest, GetSectionLocationFailsForInvalidFd) {
  EXPECT_THAT(ElfFile::GetSectionLocation(-1, ".text"), Not(IsOk()));
}

}  // namespace
}  // namespace sandbox2
