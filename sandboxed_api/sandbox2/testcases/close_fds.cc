#include <fcntl.h>
#include <linux/fs.h>
#include <unistd.h>

#include <cerrno>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/strings/numbers.h"
#include "sandboxed_api/sandbox2/sanitizer.h"

bool IsFdOpen(int fd) {
  int ret = fcntl(fd, F_GETFD);
  if (ret == -1) {
    CHECK(errno == EBADF);
    return false;
  }
  return true;
}

int main(int argc, char* argv[]) {
  absl::flat_hash_set<int> exceptions;
  for (int i = 0; i < argc; ++i) {
    int fd;
    CHECK(absl::SimpleAtoi(argv[i], &fd));
    exceptions.insert(fd);
  }
  CHECK(sandbox2::sanitizer::CloseAllFDsExcept(exceptions).ok());
  for (int i = 0; i < INR_OPEN_MAX; i++) {
    CHECK_EQ(IsFdOpen(i), exceptions.find(i) != exceptions.end());
  }
}
