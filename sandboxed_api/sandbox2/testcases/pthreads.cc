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
#include <sys/syscall.h>

#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/forkingclient.h"

pid_t GetTID() { return syscall(SYS_gettid); }

int main(int argc, char* argv[]) {
  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  sandbox2::ForkingClient s2client(&comms);

  s2client.EnterForkLoop();
  s2client.SandboxMeHere();
  sched_param param;
  param.sched_priority = 0;
  if (sched_setscheduler(GetTID(), SCHED_IDLE, &param) != 0) {
    return 1;
  }
  int policy;
  sched_param pt_param;
  if (pthread_getschedparam(pthread_self(), &policy, &pt_param) != 0) {
    return 2;
  }
  if (policy != SCHED_IDLE) {
    return 3;
  }
  return 0;
}
