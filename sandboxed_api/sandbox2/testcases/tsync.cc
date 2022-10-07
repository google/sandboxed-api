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

// A binary that starts a thread then calls SandboxMeHere.
// It is used to test tsync support.

#include <pthread.h>
#include <unistd.h>

#include <cstdio>

#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"

static pthread_barrier_t g_barrier;

void* Sleepy(void*) {
  pthread_barrier_wait(&g_barrier);
  while (true) {
    printf("hello from thread\n");
    sleep(1);
  }
}

int main(int argc, char* argv[]) {
  pthread_t thread;

  if (pthread_barrier_init(&g_barrier, nullptr, 2) < 0) {
    fprintf(stderr, "pthread_barrier_init: error\n");
    return EXIT_FAILURE;
  }

  if (pthread_create(&thread, nullptr, Sleepy, nullptr)) {
    fprintf(stderr, "pthread_create: error\n");
    return EXIT_FAILURE;
  }

  printf("hello from main\n");

  // Wait to make sure that the sleepy-thread is up and running.
  pthread_barrier_wait(&g_barrier);

  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  sandbox2::Client sandbox2_client(&comms);
  sandbox2_client.SandboxMeHere();

  return EXIT_SUCCESS;
}
