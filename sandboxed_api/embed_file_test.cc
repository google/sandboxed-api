#include "sandboxed_api/embed_file.h"

#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
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

using ::testing::_;
using ::testing::Eq;
using ::testing::HasSubstr;
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

// Returns `size` bytes of deterministic, non-repeating test data.
std::string MakeTestData(size_t size) {
  std::string data(size, '\0');
  for (size_t i = 0; i < size / sizeof(uint32_t); ++i) {
    uint32_t val = static_cast<uint32_t>(i * 0x9e3779b9u + 1);
    std::memcpy(&data[i * sizeof(uint32_t)], &val, sizeof(val));
  }
  return data;
}

// Creates an unlinked regular file (not a memfd) in the test's temporary
// directory, containing `data`, and returns an fd to it, or -1 on error.
int CreateRegularFile(absl::string_view data) {
  std::string path = absl::StrCat(
      ::testing::TempDir(), "/embed_file_test_", getpid(), "_",
      ::testing::UnitTest::GetInstance()->current_test_info()->name());
  int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd == -1) {
    return -1;
  }
  unlink(path.c_str());
  if (!file_util::fileops::WriteToFD(fd, data.data(), data.size())) {
    close(fd);
    return -1;
  }
  return fd;
}

std::string ReadAll(int fd) {
  struct stat st;
  if (fstat(fd, &st) == -1) {
    return "";
  }
  std::string data(st.st_size, '\0');
  if (pread(fd, data.data(), data.size(), 0) != st.st_size) {
    return "";
  }
  return data;
}

// Fake sendfile(2): copies up to `fake_sendfile_bytes_before_error` bytes with
// the real sendfile(2), then fails with `fake_sendfile_errno`. The state is
// thread-local so that tests running concurrently on different threads do not
// interfere with each other.
thread_local size_t fake_sendfile_bytes_before_error = 0;
thread_local int fake_sendfile_errno = 0;
thread_local size_t fake_sendfile_bytes_copied = 0;

ssize_t FakeSendfile(int out_fd, int in_fd, off_t* offset, size_t count) {
  if (fake_sendfile_bytes_copied >= fake_sendfile_bytes_before_error) {
    errno = fake_sendfile_errno;
    return -1;
  }
  count = std::min(
      count, fake_sendfile_bytes_before_error - fake_sendfile_bytes_copied);
  ssize_t n = ::sendfile(out_fd, in_fd, offset, count);
  if (n > 0) {
    fake_sendfile_bytes_copied += n;
  }
  return n;
}

void SetUpFakeSendfile(size_t bytes_before_error, int error) {
  fake_sendfile_bytes_before_error = bytes_before_error;
  fake_sendfile_errno = error;
  fake_sendfile_bytes_copied = 0;
}

constexpr size_t kTestDataSize = 5 * 1024 * 1024 + 123;
constexpr uint64_t kTestOffset = 4096 + 7;  // Unaligned on purpose.

TEST(EmbedFileTest, CopyFileToFdFromRegularFileToMemfd) {
  const std::string data = MakeTestData(kTestDataSize);
  file_util::fileops::FDCloser in_fd(CreateRegularFile(data));
  ASSERT_THAT(in_fd.get(), Ne(-1));
  ASSERT_THAT(lseek(in_fd.get(), 999, SEEK_SET), Eq(999));
  file_util::fileops::FDCloser out_fd(memfd_create("out", MFD_CLOEXEC));
  ASSERT_THAT(out_fd.get(), Ne(-1));

  const size_t size = data.size() - kTestOffset;
  ASSERT_THAT(embed_file_internal::CopyFileToFd(in_fd.get(), kTestOffset, size,
                                                out_fd.get()),
              absl_testing::IsOk());

  EXPECT_THAT(ReadAll(out_fd.get()), Eq(data.substr(kTestOffset)));
  // The file position of the input is neither used nor modified.
  EXPECT_THAT(lseek(in_fd.get(), 0, SEEK_CUR), Eq(999));
}

TEST(EmbedFileTest, CopyFileToFdIgnoresOutputFilePosition) {
  const std::string data = MakeTestData(64 * 1024);
  file_util::fileops::FDCloser in_fd(CreateRegularFile(data));
  ASSERT_THAT(in_fd.get(), Ne(-1));
  file_util::fileops::FDCloser out_fd(memfd_create("out", MFD_CLOEXEC));
  ASSERT_THAT(out_fd.get(), Ne(-1));
  ASSERT_THAT(lseek(out_fd.get(), 1000, SEEK_SET), Eq(1000));

  ASSERT_THAT(embed_file_internal::CopyFileToFd(in_fd.get(), 0, data.size(),
                                                out_fd.get()),
              absl_testing::IsOk());

  EXPECT_THAT(ReadAll(out_fd.get()), Eq(data));
}

TEST(EmbedFileTest, CopyFileToFdFallsBackMidCopy) {
  const std::string data = MakeTestData(kTestDataSize);
  file_util::fileops::FDCloser in_fd(CreateRegularFile(data));
  ASSERT_THAT(in_fd.get(), Ne(-1));
  file_util::fileops::FDCloser out_fd(memfd_create("out", MFD_CLOEXEC));
  ASSERT_THAT(out_fd.get(), Ne(-1));

  // sendfile(2) copies the first 4 KiB, then reports that it is unsupported.
  SetUpFakeSendfile(4096, EINVAL);
  const size_t size = data.size() - kTestOffset;
  ASSERT_THAT(embed_file_internal::CopyFileToFd(in_fd.get(), kTestOffset, size,
                                                out_fd.get(), &FakeSendfile),
              absl_testing::IsOk());

  EXPECT_THAT(fake_sendfile_bytes_copied, Eq(4096));
  // The fallback must continue where sendfile(2) stopped, without duplicating
  // or skipping any data.
  EXPECT_THAT(ReadAll(out_fd.get()), Eq(data.substr(kTestOffset)));
}

TEST(EmbedFileTest, CopyFileToFdFallsBackWhenUnsupported) {
  const std::string data = MakeTestData(70 * 1024);
  file_util::fileops::FDCloser in_fd(CreateRegularFile(data));
  ASSERT_THAT(in_fd.get(), Ne(-1));
  file_util::fileops::FDCloser out_fd(memfd_create("out", MFD_CLOEXEC));
  ASSERT_THAT(out_fd.get(), Ne(-1));

  SetUpFakeSendfile(0, ENOSYS);
  ASSERT_THAT(embed_file_internal::CopyFileToFd(in_fd.get(), 0, data.size(),
                                                out_fd.get(), &FakeSendfile),
              absl_testing::IsOk());

  EXPECT_THAT(ReadAll(out_fd.get()), Eq(data));
}

TEST(EmbedFileTest, CopyFileToFdReturnsHardErrors) {
  const std::string data = MakeTestData(4096);
  file_util::fileops::FDCloser in_fd(CreateRegularFile(data));
  ASSERT_THAT(in_fd.get(), Ne(-1));
  file_util::fileops::FDCloser out_fd(memfd_create("out", MFD_CLOEXEC));
  ASSERT_THAT(out_fd.get(), Ne(-1));

  SetUpFakeSendfile(0, EIO);
  EXPECT_THAT(embed_file_internal::CopyFileToFd(in_fd.get(), 0, data.size(),
                                                out_fd.get(), &FakeSendfile),
              absl_testing::StatusIs(_, HasSubstr("sendfile failed")));
  EXPECT_THAT(ReadAll(out_fd.get()), Eq(""));
}

TEST(EmbedFileTest, CopyFileToFdFailsOnShortInput) {
  const std::string data = MakeTestData(4096);
  file_util::fileops::FDCloser in_fd(CreateRegularFile(data));
  ASSERT_THAT(in_fd.get(), Ne(-1));
  file_util::fileops::FDCloser out_fd(memfd_create("out", MFD_CLOEXEC));
  ASSERT_THAT(out_fd.get(), Ne(-1));

  EXPECT_THAT(embed_file_internal::CopyFileToFd(in_fd.get(), 0, data.size() + 1,
                                                out_fd.get()),
              absl_testing::StatusIs(absl::StatusCode::kDataLoss));
}

struct SmallStackCopyArgs {
  int in_fd;
  size_t size;
  int out_fd;
  absl::Status status;
};

void* SmallStackCopy(void* arg) {
  auto* args = static_cast<SmallStackCopyArgs*>(arg);
  // Force the pread/pwrite fallback, which used to have a 32 KiB on-stack
  // buffer.
  SetUpFakeSendfile(0, EINVAL);
  args->status = embed_file_internal::CopyFileToFd(args->in_fd, 0, args->size,
                                                   args->out_fd, &FakeSendfile);
  return nullptr;
}

// Regression test for b/571598741: copying must work on threads with small
// stacks, e.g. fibers.
TEST(EmbedFileTest, CopyFileToFdWorksOnSmallStack) {
  const std::string data = MakeTestData(100 * 1024);
  file_util::fileops::FDCloser in_fd(CreateRegularFile(data));
  ASSERT_THAT(in_fd.get(), Ne(-1));
  file_util::fileops::FDCloser out_fd(memfd_create("out", MFD_CLOEXEC));
  ASSERT_THAT(out_fd.get(), Ne(-1));

  SmallStackCopyArgs args = {in_fd.get(), data.size(), out_fd.get(),
                             absl::UnknownError("not run")};
  pthread_attr_t attr;
  ASSERT_THAT(pthread_attr_init(&attr), Eq(0));
  const size_t stack_size = std::max<size_t>(
      32 * 1024, static_cast<size_t>(sysconf(_SC_THREAD_STACK_MIN)));
  ASSERT_THAT(pthread_attr_setstacksize(&attr, stack_size), Eq(0));
  pthread_t thread;
  ASSERT_THAT(pthread_create(&thread, &attr, &SmallStackCopy, &args), Eq(0));
  ASSERT_THAT(pthread_join(thread, nullptr), Eq(0));
  pthread_attr_destroy(&attr);

  EXPECT_THAT(args.status, absl_testing::IsOk());
  EXPECT_THAT(ReadAll(out_fd.get()), Eq(data));
}

TEST(EmbedFileTest, FallbackChunkedCopyMultiChunk) {
  const std::string data = MakeTestData(70 * 1024);  // Spans > 2 chunks.
  file_util::fileops::FDCloser in_fd(memfd_create("in", MFD_CLOEXEC));
  ASSERT_THAT(in_fd.get(), Ne(-1));
  ASSERT_TRUE(
      file_util::fileops::WriteToFD(in_fd.get(), data.data(), data.size()));
  file_util::fileops::FDCloser out_fd(memfd_create("out", MFD_CLOEXEC));
  ASSERT_THAT(out_fd.get(), Ne(-1));

  constexpr uint64_t kOffset = 1024;
  constexpr size_t kCopySize = 65 * 1024;
  constexpr uint64_t kOutOffset = 100;
  ASSERT_THAT(embed_file_internal::FallbackChunkedCopy(
                  in_fd.get(), kOffset, kCopySize, out_fd.get(), kOutOffset),
              absl_testing::IsOk());

  EXPECT_THAT(ReadAll(out_fd.get()), Eq(std::string(kOutOffset, '\0') +
                                        data.substr(kOffset, kCopySize)));
}

TEST(EmbedFileTest, FailedSectionCopyFallsBackToDataCleanly) {
  constexpr EmbedToc kToc = {
      .name = "section_with_data_fallback",
      .data = "fallback data",
      .section_name = ".non_existent_section",
  };
  std::unique_ptr<EmbedFile> embed_file = EmbedFileTestPeer::NewInstance();
  int fd = embed_file->GetFdForFileToc(kToc);
  ASSERT_THAT(fd, Ne(-1));
  EXPECT_THAT(ReadAll(fd), Eq("fallback data"));
}

}  // namespace
}  // namespace sapi
