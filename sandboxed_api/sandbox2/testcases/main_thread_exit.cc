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

// A binary where the main thread (thread group leader) spawns a worker thread
// and exits via SYS_exit (not SYS_exit_group), leaving the worker thread to
// trigger process termination.

#include <linux/futex.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <csignal>

#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/network_proxy/client.h"
#include "sandboxed_api/util/raw_logging.h"

namespace {

std::atomic<int> g_main_tid{1};

void WaitForMainThreadExit() {
  while (g_main_tid.load(std::memory_order_acquire) != 0) {
    syscall(__NR_futex, &g_main_tid, FUTEX_WAIT, 1, nullptr, nullptr, 0);
  }
}

void* WorkerThread(void* arg) {
  absl::string_view mode = static_cast<const char*>(arg);
  WaitForMainThreadExit();

  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  if (mode == "network_violation") {
    sandbox2::Client client(&comms);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    SAPI_RAW_PCHECK(s >= 0, "socket");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    client.GetNetworkProxyClient()
        ->Connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr))
        .IgnoreError();
    pause();
  } else if (mode == "wait") {
    SAPI_RAW_CHECK(comms.SendBool(true), "SendBool");
    pause();
  } else if (mode == "signal") {
    kill(getpid(), SIGTERM);
    pause();
  }
  return nullptr;
}

}  // namespace

int main(int argc, char* argv[]) {
  SAPI_RAW_CHECK(argc >= 2, "missing mode argument");
  pthread_t thread;
  SAPI_RAW_CHECK(pthread_create(&thread, nullptr, WorkerThread, argv[1]) == 0,
                 "pthread_create");
  syscall(__NR_set_tid_address, &g_main_tid);
  syscall(__NR_exit, 0);
}
