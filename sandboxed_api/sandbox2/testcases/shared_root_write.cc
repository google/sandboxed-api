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

// Testcase for SharedMountNamespaceLeaksWritableRoot: writes a marker file
// to the sandboxee's own writable root filesystem.

#include <cstdio>

int main() {
  FILE* f = fopen("/shared_root_marker", "w");
  if (f == nullptr) {
    return 2;
  }
  fputs("written-by-instance-A\n", f);
  fclose(f);
  return 0;
}
