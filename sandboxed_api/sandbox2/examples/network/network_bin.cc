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

// This file is an example of a network sandboxed binary inside a network
// namespace. It can't connect with the server directly, but the executor can
// establish a connection and pass the connected socket to the sandboxee.

#include <sys/socket.h>
#include <syscall.h>

#include <cstring>
#include <string>

#include "absl/log/log.h"
#include "absl/strings/str_format.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"

static ssize_t ReadFromFd(int fd, uint8_t* buf, size_t size) {
  ssize_t received = 0;
  while (received < size) {
    ssize_t read_status =
        TEMP_FAILURE_RETRY(read(fd, &buf[received], size - received));
    if (read_status == 0) {
      break;
    }
    if (read_status < 0) {
      return -1;
    }
    received += read_status;
  }
  return received;
}

static bool CommunicationTest(int sock) {
  char received[1025] = {0};

  if (ReadFromFd(sock, reinterpret_cast<uint8_t*>(received),
                 sizeof(received) - 1) <= 0) {
    LOG(ERROR) << "Data receiving error";
    return false;
  }
  absl::PrintF("Sandboxee received data from the server:\n\n%s\n", received);
  if (strcmp(received, "Hello World\n")) {
    LOG(ERROR) << "Data receiving error";
    return false;
  }

  return true;
}

int main(int argc, char* argv[]) {
  // Set-up the sandbox2::Client object, using a file descriptor (1023).
  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  sandbox2::Client sandbox2_client(&comms);
  // Enable sandboxing from here.
  sandbox2_client.SandboxMeHere();

  int client;
  if (!comms.RecvFD(&client)) {
    fputs("sandboxee: !comms.RecvFD(&client) failed\n", stderr);
    return 1;
  }

  if (!CommunicationTest(client)) {
    return 2;
  }
  return 0;
}
