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

// A binary that crashes, either directly or by copying and re-executing,
// to test the stack tracing symbolizer.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "absl/base/attributes.h"
#include "absl/strings/numbers.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/temp_file.h"

ABSL_ATTRIBUTE_NOINLINE
void CrashMe() {
  volatile char* null = nullptr;
  *null = 0;
}

void RunWritable() {
  int exe_fd = open("/proc/self/exe", O_RDONLY);
  SAPI_RAW_PCHECK(exe_fd >= 0, "Opening /proc/self/exe");

  std::string tmpname;
  int tmp_fd;
  std::tie(tmpname, tmp_fd) = sapi::CreateNamedTempFile("tmp").value();
  SAPI_RAW_PCHECK(fchmod(tmp_fd, S_IRWXU) == 0, "Fchmod on temporary file");

  char buf[4096];
  while (true) {
    ssize_t read_cnt = read(exe_fd, buf, sizeof(buf));
    SAPI_RAW_PCHECK(read_cnt >= 0, "Reading /proc/self/exe");
    if (read_cnt == 0) {
      break;
    }
    SAPI_RAW_PCHECK(write(tmp_fd, buf, read_cnt) == read_cnt,
                    "Writing temporary file");
  }

  SAPI_RAW_PCHECK(close(tmp_fd) == 0, "Closing temporary file");
  tmp_fd = open(tmpname.c_str(), O_RDONLY);
  SAPI_RAW_PCHECK(tmp_fd >= 0, "Reopening temporary file");

  char prog_name[] = "crashme";
  char testno[] = "1";
  char* argv[] = {prog_name, testno, nullptr};

  SAPI_RAW_PCHECK(execv(tmpname.c_str(), argv) == 0, "Executing copied binary");
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printf("argc < 3\n");
    return EXIT_FAILURE;
  }

  int testno;
  SAPI_RAW_CHECK(absl::SimpleAtoi(argv[1], &testno), "testno not a number");
  switch (testno) {
    case 1:
      CrashMe();
      break;
    case 2:
      RunWritable();
      break;
    default:
      printf("Unknown test: %d\n", testno);
      return EXIT_FAILURE;
  }

  printf("OK: All tests went OK\n");
  return EXIT_SUCCESS;
}
