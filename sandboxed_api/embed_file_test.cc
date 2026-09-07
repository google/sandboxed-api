#include "sandboxed_api/embed_file.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/embed_toc.h"
#include "sandboxed_api/util/fileops.h"

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

TEST(EmbedFileTest, GetSectionFd) {
  constexpr EmbedToc kSectionToc = {
      .name = "text_section",
      .data = "",
      .section_name = ".text",
  };
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(kSectionToc);
  EXPECT_THAT(fd, Ne(-1));
  if (fd != -1) {
    char buf[16];
    EXPECT_THAT(read(fd, buf, sizeof(buf)), Eq(sizeof(buf)));
  }
}

TEST(EmbedFileTest, NonExistentSectionFails) {
  constexpr EmbedToc kInvalidSectionToc = {
      .name = "non_existent",
      .data = "",
      .section_name = ".non_existent_section",
  };
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(kInvalidSectionToc);
  EXPECT_THAT(fd, Eq(-1));
}

TEST(EmbedFileTest, FallbackChunkedCopyMultiChunk) {
  constexpr size_t kTotalSize =
      70 * 1024;  // 70 KB, spans > 2 chunks (32 KB each)
  std::string src_data;
  src_data.resize(kTotalSize);
  for (size_t i = 0; i < kTotalSize / sizeof(uint32_t); ++i) {
    uint32_t val = static_cast<uint32_t>(i * 0x9e3779b9u + 1);
    std::memcpy(&src_data[i * sizeof(uint32_t)], &val, sizeof(val));
  }

  int in_fd = memfd_create("fallback_in", MFD_CLOEXEC);
  ASSERT_THAT(in_fd, Ne(-1));
  file_util::fileops::FDCloser in_closer(in_fd);
  ASSERT_TRUE(
      file_util::fileops::WriteToFD(in_fd, src_data.data(), src_data.size()));

  int out_fd = memfd_create("fallback_out", MFD_CLOEXEC);
  ASSERT_THAT(out_fd, Ne(-1));
  file_util::fileops::FDCloser out_closer(out_fd);

  constexpr uint64_t kOffset = 1024;
  constexpr size_t kCopySize = 65 * 1024;  // 65 KB
  ASSERT_THAT(internal::FallbackChunkedCopy(in_fd, kOffset, kCopySize, out_fd),
              absl_testing::IsOk());

  std::string dst_data(kCopySize, '\0');
  ssize_t read_bytes = pread(out_fd, &dst_data[0], kCopySize, 0);
  EXPECT_THAT(read_bytes, Eq(kCopySize));
  EXPECT_THAT(dst_data, Eq(src_data.substr(kOffset, kCopySize)));
}

}  // namespace
}  // namespace sapi
