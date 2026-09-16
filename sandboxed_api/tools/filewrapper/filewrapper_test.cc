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

#include <dlfcn.h>
#include <unistd.h>

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/embed_file.h"
#include "sandboxed_api/embed_toc.h"
#include "sandboxed_api/sandbox2/util/minielf.h"
#include "sandboxed_api/testing.h"
#include "sandboxed_api/tools/filewrapper/filewrapper_embedded.h"

namespace sapi {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::NotNull;
using ::testing::StartsWith;
using ::testing::StrEq;

TEST(FilewrapperTest, BasicFunctionality) {
  auto raw_toc = filewrapper_embedded_create();
  ASSERT_THAT(raw_toc, NotNull());
  sapi::EmbedToc toc = sapi::EmbedToc::From(*raw_toc);

  EXPECT_THAT(std::string(toc.name), StrEq("filewrapper_embedded.bin"));

  constexpr absl::string_view kExpectedPayload =
      "filewrapper test embedded payload\n";

  if (!toc.section_name.empty()) {
    EXPECT_THAT(std::string(toc.section_name),
                StartsWith(".sapi_embed_filewrapper_embedded_bin_"));
    const char* elf_path = "/proc/self/exe";
    Dl_info dlinfo;
    if (dladdr(raw_toc, &dlinfo) != 0 && dlinfo.dli_fname != nullptr &&
        dlinfo.dli_fname[0] != '\0') {
      elf_path = dlinfo.dli_fname;
    }
    SAPI_ASSERT_OK_AND_ASSIGN(auto loc, sandbox2::ElfFile::GetSectionLocation(
                                            elf_path, toc.section_name));
    EXPECT_THAT(loc.offset % 4096, Eq(0));
  } else {
    EXPECT_THAT(std::string(toc.data), StrEq(kExpectedPayload));
  }

  int fd = EmbedFile::instance()->GetFdForFileToc(toc);
  ASSERT_THAT(fd, Ne(-1));

  std::string materialized_contents(kExpectedPayload.size(), '\0');
  EXPECT_THAT(pread(fd, &materialized_contents[0], kExpectedPayload.size(), 0),
              Eq(kExpectedPayload.size()));
  EXPECT_THAT(materialized_contents, StrEq(kExpectedPayload));
}

}  // namespace
}  // namespace sapi
