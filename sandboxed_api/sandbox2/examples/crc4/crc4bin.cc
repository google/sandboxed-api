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

// This file is an example of a computational-centric binary which is intended
// to be sandboxed by the sandbox2.

#include <syscall.h>

#include <cstdint>
#include <cstring>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "sandboxed_api/sandbox2/client.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/util.h"

ABSL_FLAG(bool, call_syscall_not_allowed, false,
          "Call a syscall that is not allowed by policy.");

// This function is insecure (i.e. can be crashed and exploited) to demonstrate
// how sandboxing can be helpful in defending against bugs.
// We need to make sure that this function is not inlined, so that we don't
// optimize the bug away.
static uint32_t ComputeCRC4Impl(const uint8_t* ptr, uint64_t len) {
  uint8_t buf[8] = {0};

  memcpy(buf, ptr, len);  // buffer overflow!

  uint32_t crc4 = 0;
  for (uint64_t i = 0; i < len; i++) {
    crc4 ^= (buf[i] << (i % 4) * 8);
  }
  return crc4;
}

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);

  // Set-up the sandbox2::Client object, using a file descriptor (1023).
  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  sandbox2::Client sandbox2_client(&comms);
  // Enable sandboxing from here.
  sandbox2_client.SandboxMeHere();

  // A syscall not allowed in policy, should cause a violation.
  if (absl::GetFlag(FLAGS_call_syscall_not_allowed)) {
    sandbox2::util::Syscall(__NR_sendfile, 0, 0, 0, 0, 0, 0);
  }

  // Receive data to be processed, process it, and return results.
  std::vector<uint8_t> buffer;
  if (!comms.RecvBytes(&buffer)) {
    return 1;
  }

  // Make sure we don't inline the function. See the comment in
  // ComputeCRC4Impl() for more details.
  std::function<uint32_t(const uint8_t*, uint64_t)> ComputeCRC4 =
      ComputeCRC4Impl;

  uint32_t crc4 = ComputeCRC4(buffer.data(), buffer.size());

  if (!comms.SendUint32(crc4)) {
    return 2;
  }
  return 0;
}
