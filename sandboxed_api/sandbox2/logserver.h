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

#ifndef SANDBOXED_API_SANDBOX2_LOGSERVER_H_
#define SANDBOXED_API_SANDBOX2_LOGSERVER_H_

#include "sandboxed_api/sandbox2/comms.h"

namespace sandbox2 {

// The LogServer waits for messages from the sandboxee on a given file
// descriptor and logs them using the standard logging facilities.
class LogServer {
 public:
  explicit LogServer(int fd);

  LogServer(const LogServer&) = delete;
  LogServer& operator=(const LogServer&) = delete;

  // Starts handling incoming log messages.
  void Run();

 private:
  Comms comms_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_LOGSERVER_H_
