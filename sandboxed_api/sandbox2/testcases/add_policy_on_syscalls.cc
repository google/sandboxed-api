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

// A binary that tests a lot of syscalls, to test the AddPolicyOnSyscall
// functionality.

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>

int main() {
  unsigned int r, e, s;
  char buf[1];
  // 1000 is the UID/GID we use inside the namespaces.
  if (getuid() != 1000) return 1;
  if (getgid() != 1000) return 2;
  if (geteuid() != 1000) return 3;
  if (getegid() != 1000) return 4;
  if (getresuid(&r, &e, &s) != -1 || errno != 42) return 5;
  if (getresgid(&r, &e, &s) != -1 || errno != 42) return 6;
  if (read(0, buf, 1) != -1 || errno != 43) return 7;
  if (write(1, buf, 1) != -1 || errno != 43) return 8;

  // Trigger a violation.
  umask(0);
  return 0;
}
