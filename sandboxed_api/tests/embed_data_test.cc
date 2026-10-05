// Copyright 2026 Google LLC
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

#include <dlfcn.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>

#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/embed_file.h"
#include "sandboxed_api/embed_toc.h"
#include "sandboxed_api/sandbox2/util/minielf.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/tests/unmapped_embedded_data.h"
#include "sandboxed_api/util/fileops.h"

namespace sapi {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::StartsWith;
using ::testing::StrEq;

constexpr absl::string_view kExpectedPayload =
    "SAPI embedded test payload data 0123456789\n";

TEST(EmbedDataTest, UnmappedElfEmbedding) {
  const auto* raw_toc = unmapped_embedded_data_create();
  ASSERT_THAT(raw_toc, Ne(nullptr));

  sapi::EmbedToc toc = sapi::EmbedToc::From(*raw_toc);
  EXPECT_THAT(std::string(toc.name), StrEq("embedded_data.bin"));

  if (!toc.section_name.empty()) {
    EXPECT_THAT(std::string(toc.section_name),
                StartsWith(".sapi_embed_embedded_data_bin_"));

    const char* elf_path = "/proc/self/exe";
    Dl_info dlinfo;
    if (dladdr(raw_toc, &dlinfo) != 0 && dlinfo.dli_fname != nullptr &&
        dlinfo.dli_fname[0] != '\0') {
      elf_path = dlinfo.dli_fname;
    }
    SAPI_ASSERT_OK_AND_ASSIGN(auto loc, sandbox2::ElfFile::GetSectionLocation(
                                            elf_path, toc.section_name));
    EXPECT_THAT(loc.offset % 4096, Eq(0));
  }

  int fd = EmbedFile::instance()->GetFdForFileToc(toc);
  ASSERT_THAT(fd, Ne(-1));

  std::string materialized_contents(kExpectedPayload.size(), '\0');
  EXPECT_THAT(pread(fd, &materialized_contents[0], kExpectedPayload.size(), 0),
              Eq(kExpectedPayload.size()));
  EXPECT_THAT(materialized_contents, StrEq(kExpectedPayload));
}

// Runs `binary` with `argv0` as argv[0]. Returns the exit code, or -1 if the
// process could not be spawned or did not exit normally.
int RunWithArgv0(const std::string& binary, std::string argv0) {
  char* const argv[] = {argv0.data(), nullptr};
  pid_t pid;
  if (posix_spawn(&pid, binary.c_str(), nullptr, nullptr, argv, environ) != 0) {
    return -1;
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)) {
    return -1;
  }
  return WEXITSTATUS(status);
}

// For the main program, glibc's dladdr(3) reports argv[0] as the file name,
// which is controlled by whoever executes the binary. Locating unmapped
// sections must not depend on it.
// embed_argv0_helper exits with 1 if it fails to materialize the payload and
// with 2 if it materializes the wrong contents.
TEST(EmbedDataTest, UnmappedLookupIgnoresArgv0NamingADirectory) {
  if (EmbedToc::From(*unmapped_embedded_data_create()).section_name.empty()) {
    GTEST_SKIP() << "Test data is not embedded in an unmapped ELF section";
  }
  EXPECT_THAT(RunWithArgv0(GetTestSourcePath("tests/embed_argv0_helper"),
                           GetTestTempPath()),
              Eq(0));
}

TEST(EmbedDataTest, UnmappedLookupIgnoresArgv0NamingAnotherBinary) {
  const EmbedToc toc = EmbedToc::From(*unmapped_embedded_data_create());
  if (toc.section_name.empty()) {
    GTEST_SKIP() << "Test data is not embedded in an unmapped ELF section";
  }
  // Create a copy of the helper with a clobbered payload section.
  const std::string helper = GetTestSourcePath("tests/embed_argv0_helper");
  const std::string decoy = GetTestTempPath("embed_argv0_helper_decoy");
  ASSERT_TRUE(file_util::fileops::CopyFile(helper, decoy, 0755));
  file_util::fileops::FDCloser decoy_fd(
      open(decoy.c_str(), O_RDWR | O_CLOEXEC));
  ASSERT_THAT(decoy_fd.get(), Ne(-1));
  SAPI_ASSERT_OK_AND_ASSIGN(
      sandbox2::ElfSectionLocation loc,
      sandbox2::ElfFile::GetSectionLocation(decoy_fd.get(), toc.section_name));
  const std::string garbage(loc.size, 'X');
  ASSERT_THAT(
      pwrite(decoy_fd.get(), garbage.data(), garbage.size(), loc.offset),
      Eq(garbage.size()));
  ASSERT_TRUE(decoy_fd.Close());

  EXPECT_THAT(RunWithArgv0(helper, decoy), Eq(0));
}

void BM_LoadEmbeddedFileEndToEnd(benchmark::State& state) {
  std::string helper_path = GetTestSourcePath("tests/embed_benchmark_helper");
  char* const argv[] = {const_cast<char*>(helper_path.c_str()), nullptr};

  for (auto _ : state) {
    pid_t pid;
    int err =
        posix_spawn(&pid, helper_path.c_str(), nullptr, nullptr, argv, environ);
    CHECK_EQ(err, 0);

    int status = 0;
    CHECK_EQ(waitpid(pid, &status, 0), pid);
    CHECK(WIFEXITED(status));
    CHECK_EQ(WEXITSTATUS(status), 0);
  }
}
BENCHMARK(BM_LoadEmbeddedFileEndToEnd)->UseRealTime();

}  // namespace
}  // namespace sapi
