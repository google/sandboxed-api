#include "sandboxed_api/embed_file.h"

#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/embed_toc.h"

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
constexpr EmbedToc kRegularToc = {
    .name = "regular",
    .data = kRegularContents,
};

constexpr EmbedToc kEmptyToc = {
    .name = "empty",
    .data = "",
};

TEST(EmbedFileTest, GetRegularFd) {
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(kRegularToc);
  EXPECT_THAT(fd, Ne(-1));
}

TEST(EmbedFileTest, DuplicateGetFdIsSame) {
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(kRegularToc);
  EXPECT_THAT(fd, Ne(-1));
  int fd2 = embed_file->GetFdForFileToc(kRegularToc);
  EXPECT_THAT(fd, Eq(fd2));
}

TEST(EmbedFileTest, GetDupFdReturnsFreshFd) {
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(kRegularToc);
  EXPECT_THAT(fd, Ne(-1));
  int dup_fd = embed_file->GetDupFdForFileToc(kRegularToc);
  EXPECT_THAT(fd, Ne(dup_fd));
  close(dup_fd);
}

TEST(EmbedFileTest, EmptyTocSucceeds) {
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(kEmptyToc);
  EXPECT_THAT(fd, Ne(-1));
  int dup_fd = embed_file->GetDupFdForFileToc(kEmptyToc);
  EXPECT_THAT(dup_fd, Ne(-1));
  close(dup_fd);
}

TEST(EmbedFileTest, SameNameDifferentDataCachesSeparately) {
  constexpr EmbedToc kToc1 = {.name = "shared_name", .data = "data1"};
  constexpr EmbedToc kToc2 = {.name = "shared_name", .data = "data2"};
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd1 = embed_file->GetFdForFileToc(kToc1);
  int fd2 = embed_file->GetFdForFileToc(kToc2);
  EXPECT_THAT(fd1, Ne(-1));
  EXPECT_THAT(fd2, Ne(-1));
  EXPECT_THAT(fd1, Ne(fd2));
}

TEST(EmbedFileTest, OverlongNameTocFails) {
  std::string overlong_name(1000, 'a');
  EmbedToc overlong_name_toc = {
      .name = overlong_name,
      .data = kRegularContents,
  };
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(overlong_name_toc);
  EXPECT_THAT(fd, Eq(-1));
}

}  // namespace
}  // namespace sapi
