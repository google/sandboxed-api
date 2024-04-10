#include "sandboxed_api/embed_file.h"

#include <memory>
#include <string>

#include "sandboxed_api/file_toc.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"

namespace sapi {

class EmbedFileTestPeer {
 public:
  static std::unique_ptr<EmbedFile> NewInstance() {
    return absl::WrapUnique(new EmbedFile());
  }
};

namespace {

using ::testing::Eq;
using ::testing::Ne;

constexpr absl::string_view kRegularContents = "Hello world!";
constexpr FileToc kRegularToc = {
    .name = "regular",
    .data = kRegularContents.data(),
    .size = kRegularContents.size(),
    .md5digest = {},  // MD5 is unused in SAPI implementation
};

constexpr FileToc kFaultyToc = {
    .name = "regular",
    .data = nullptr,
    .size = 100,
    .md5digest = {},  // MD5 is unused in SAPI implementation
};

TEST(EmbedFileTest, GetRegularFd) {
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(&kRegularToc);
  EXPECT_THAT(fd, Ne(-1));
}

TEST(EmbedFileTest, DuplicateGetFdIsSame) {
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(&kRegularToc);
  EXPECT_THAT(fd, Ne(-1));
  int fd2 = embed_file->GetFdForFileToc(&kRegularToc);
  EXPECT_THAT(fd, Eq(fd2));
}

TEST(EmbedFileTest, GetDupFdReturnsFreshFd) {
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(&kRegularToc);
  EXPECT_THAT(fd, Ne(-1));
  int dup_fd = embed_file->GetDupFdForFileToc(&kRegularToc);
  EXPECT_THAT(fd, Ne(dup_fd));
  close(dup_fd);
}

TEST(EmbedFileTest, FaultyTocFails) {
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(&kFaultyToc);
  EXPECT_THAT(fd, Eq(-1));
  int dup_fd = embed_file->GetDupFdForFileToc(&kFaultyToc);
  EXPECT_THAT(dup_fd, Eq(-1));
}

TEST(EmbedFileTest, OverlongNameTocFails) {
  std::string overlong_name(1000, 'a');
  FileToc overlong_name_toc = {
      .name = overlong_name.c_str(),
      .data = kRegularContents.data(),
      .size = kRegularContents.size(),
      .md5digest = {},  // MD5 is unused in SAPI implementation
  };
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(&overlong_name_toc);
  EXPECT_THAT(fd, Eq(-1));
}

}  // namespace
}  // namespace sapi
