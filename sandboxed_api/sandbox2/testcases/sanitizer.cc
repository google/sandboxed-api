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

// A binary that tests for opened or closed file descriptors as specified.

#include <fcntl.h>
#include <linux/fs.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The fd provided should not be open.
void TestClosedFd(int fd) {
  int ret = fcntl(fd, F_GETFD);
  if (ret != -1) {
    printf("FAILURE: FD:%d is not closed\n", fd);
    exit(EXIT_FAILURE);
  }
  if (errno != EBADF) {
    printf(
        "FAILURE: fcntl(%d) returned errno=%d (%s), should have "
        "errno=EBADF/%d (%s)\n",
        fd, errno, strerror(errno), EBADF, strerror(EBADF));
    exit(EXIT_FAILURE);
  }
}

// The fd provided should be open.
void TestOpenFd(int fd) {
  int ret = fcntl(fd, F_GETFD);
  if (ret == -1) {
    printf("FAILURE: fcntl(%d) returned -1 with errno=%d (%s)\n", fd, errno,
           strerror(errno));
    exit(EXIT_FAILURE);
  }
}

int main(int argc, char* argv[]) {
  // Disable caching.
  setbuf(stdin, nullptr);
  setbuf(stdout, nullptr);
  setbuf(stderr, nullptr);

  for (int i = 0; i <= INR_OPEN_MAX; i++) {
    bool should_be_closed = true;
    for (int j = 0; j < argc; j++) {
      errno = 0;
      int cur_fd = strtol(argv[j], nullptr, 10);  // NOLINT
      if (errno != 0) {
        printf("FAILURE: strtol('%s') failed\n", argv[j]);
        exit(EXIT_FAILURE);
      }
      if (i == cur_fd) {
        should_be_closed = false;
      }
    }

    printf("%d:%c ", i, should_be_closed ? 'C' : 'O');

    if (should_be_closed) {
      TestClosedFd(i);
    } else {
      TestOpenFd(i);
    }
  }

  printf("OK: All tests went OK\n");
  return EXIT_SUCCESS;
}
