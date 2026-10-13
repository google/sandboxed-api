// Copyright 2026 Google LLC
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

// A binary that spawns threads concurrently with SandboxMeHere.
// Used to test PtraceMonitor::InitPtraceAttach when already-seized threads
// clone new threads via PTRACE_O_TRACECLONE during attach.

#include <pthread.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"

static pthread_barrier_t g_barrier;
static pthread_barrier_t g_done_barrier;
static std::atomic<bool> g_sandboxed{false};

void* NoopThread(void*) { return nullptr; }

void* Spawner(void*) {
  pthread_barrier_wait(&g_barrier);
  while (!g_sandboxed.load(std::memory_order_relaxed)) {
    pthread_t child;
    if (pthread_create(&child, nullptr, NoopThread, nullptr) == 0) {
      pthread_join(child, nullptr);
    }
  }
  return nullptr;
}

void* IdleThread(void*) {
  pthread_barrier_wait(&g_barrier);
  pthread_barrier_wait(&g_done_barrier);
  return nullptr;
}

int main(int argc, char* argv[]) {
  constexpr int kNumIdleThreads = 25;
  constexpr int kNumSpawnerThreads = 4;
  if (pthread_barrier_init(&g_barrier, nullptr,
                           1 + kNumIdleThreads + kNumSpawnerThreads) < 0) {
    fprintf(stderr, "pthread_barrier_init: error\n");
    return EXIT_FAILURE;
  }
  if (pthread_barrier_init(&g_done_barrier, nullptr, 1 + kNumIdleThreads) < 0) {
    fprintf(stderr, "pthread_barrier_init: error\n");
    return EXIT_FAILURE;
  }

  std::vector<pthread_t> threads(kNumIdleThreads + kNumSpawnerThreads);
  for (int i = 0; i < kNumIdleThreads; ++i) {
    if (pthread_create(&threads[i], nullptr, IdleThread, nullptr)) {
      fprintf(stderr, "pthread_create: error\n");
      return EXIT_FAILURE;
    }
  }
  for (int i = 0; i < kNumSpawnerThreads; ++i) {
    if (pthread_create(&threads[kNumIdleThreads + i], nullptr, Spawner,
                       nullptr)) {
      fprintf(stderr, "pthread_create: error\n");
      return EXIT_FAILURE;
    }
  }

  pthread_barrier_wait(&g_barrier);

  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  sandbox2::Client sandbox2_client(&comms);
  sandbox2_client.SandboxMeHere();
  g_sandboxed.store(true, std::memory_order_relaxed);
  pthread_barrier_wait(&g_done_barrier);

  for (pthread_t t : threads) {
    pthread_join(t, nullptr);
  }
  return EXIT_SUCCESS;
}
