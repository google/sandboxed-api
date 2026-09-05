// Copyright 2026 The Sandboxed API Authors
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

// Testcase for SharedMountNamespaceLeaksWritableRoot: checks whether the
// marker file written by shared_root_write is visible from a separate
// Sandbox2 instance.

#include <cstdio>
#include <cstring>

int main() {
  FILE* f = fopen("/shared_root_marker", "r");
  if (f == nullptr) {
    return 1;  // Not found -- root is properly isolated.
  }
  char buf[128] = {0};
  fgets(buf, sizeof(buf), f);
  fclose(f);
  return strcmp(buf, "written-by-instance-A\n") == 0 ? 0 : 3;
}
