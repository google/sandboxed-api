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

#include <sched.h>
#include <signal.h>
#include <unistd.h>

#include <cinttypes>

constexpr int kProcesses = 512;
constexpr int kThreads = 1;
constexpr int kSenders = 1;
constexpr int kStackSize = 4096;

pid_t g_pids[kProcesses];

uint8_t g_stacks[kThreads][kStackSize] __attribute__((aligned(4096)));
constexpr int kSignals[] = {
    SIGPROF,
};

void SignalHandler(int) {}

int ChildFunc(void*) {
  for (;;) {
    sleep(10);
  }
}

int main() {
  for (int i = 0; i < kProcesses; ++i) {
    int p[2];
    char c;
    pipe(p);
    g_pids[i] = fork();
    if (g_pids[i] == 0) {
      for (int sig : kSignals) {
        signal(sig, SignalHandler);
      }
      for (int j = 0; j < kThreads; ++j) {
        int flags = CLONE_FILES | CLONE_FS | CLONE_IO | CLONE_PARENT |
                    CLONE_SIGHAND | CLONE_THREAD | CLONE_VM;
        clone(&ChildFunc, g_stacks[j + 1], flags, nullptr, nullptr, nullptr,
              nullptr);
      }
      close(p[0]);
      write(p[1], &c, 1);
      close(p[1]);
      for (;;) {
        sleep(10);
      }
    }
    read(p[0], &c, 1);
  }
  for (int i = 0; i < kSenders; ++i) {
    if (fork() == 0) {
      break;
    }
  }
  for (;;) {
    for (int sig : kSignals) {
      for (int pid : g_pids) {
        kill(pid, sig);
      }
    }
  }
}
