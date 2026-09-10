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
#include "sandboxed_api/tools/filewrapper/filewrapper.h"
#include "sandboxed_api/tools/filewrapper/filewrapper_embedded.h"

namespace sapi {
namespace {

using ::testing::Eq;
using ::testing::Ne;
using ::testing::NotNull;
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
                StrEq(".sapi_embed_filewrapper_embedded_bin"));
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

TEST(FilewrapperTest, ValidatesCppIdentifier) {
  // Valid identifiers
  EXPECT_TRUE(IsValidCppIdentifier("foo"));
  EXPECT_TRUE(IsValidCppIdentifier("_foo"));
  EXPECT_TRUE(IsValidCppIdentifier("foo_123"));
  EXPECT_TRUE(IsValidCppIdentifier("ABC"));
  EXPECT_TRUE(IsValidCppIdentifier("_"));

  // Invalid identifiers
  EXPECT_FALSE(IsValidCppIdentifier(""));
  EXPECT_FALSE(IsValidCppIdentifier("123foo"));
  EXPECT_FALSE(IsValidCppIdentifier("foo-bar"));
  EXPECT_FALSE(IsValidCppIdentifier("foo bar"));
  EXPECT_FALSE(IsValidCppIdentifier("foo.bar"));
  EXPECT_FALSE(IsValidCppIdentifier("foo::bar"));

  // Keywords
  EXPECT_FALSE(IsValidCppIdentifier("class"));
  EXPECT_FALSE(IsValidCppIdentifier("namespace"));
  EXPECT_FALSE(IsValidCppIdentifier("struct"));
  EXPECT_FALSE(IsValidCppIdentifier("int"));
  EXPECT_FALSE(IsValidCppIdentifier("return"));
  EXPECT_FALSE(IsValidCppIdentifier("template"));
  EXPECT_FALSE(IsValidCppIdentifier("typename"));
  EXPECT_FALSE(IsValidCppIdentifier("const"));
  EXPECT_FALSE(IsValidCppIdentifier("default"));
}

TEST(FilewrapperTest, ValidatesNamespace) {
  // Valid namespaces
  EXPECT_TRUE(IsValidNamespace("sapi"));
  EXPECT_TRUE(IsValidNamespace("foo"));
  EXPECT_TRUE(IsValidNamespace("_foo_123"));
  EXPECT_TRUE(IsValidNamespace("sapi::tools::filewrapper"));
  EXPECT_TRUE(IsValidNamespace("foo::bar::baz"));
  EXPECT_TRUE(IsValidNamespace("A::B::C"));

  // Invalid namespaces
  EXPECT_FALSE(IsValidNamespace(""));
  EXPECT_FALSE(IsValidNamespace("::foo"));
  EXPECT_FALSE(IsValidNamespace("foo::"));
  EXPECT_FALSE(IsValidNamespace("foo::::bar"));
  EXPECT_FALSE(IsValidNamespace("123foo"));
  EXPECT_FALSE(IsValidNamespace("foo-bar"));
  EXPECT_FALSE(IsValidNamespace("foo.bar"));
  EXPECT_FALSE(IsValidNamespace("foo bar"));
  EXPECT_FALSE(IsValidNamespace("foo::123"));
  EXPECT_FALSE(IsValidNamespace("foo::bar::"));

  // Namespaces containing C++ keywords
  EXPECT_FALSE(IsValidNamespace("class"));
  EXPECT_FALSE(IsValidNamespace("int"));
  EXPECT_FALSE(IsValidNamespace("namespace"));
  EXPECT_FALSE(IsValidNamespace("sapi::namespace::foo"));
  EXPECT_FALSE(IsValidNamespace("sapi::return"));
  EXPECT_FALSE(IsValidNamespace("sapi::default::bar"));
}

}  // namespace
}  // namespace sapi
