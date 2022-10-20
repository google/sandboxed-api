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

#include "sandboxed_api/sandbox2/logserver.h"

#include <string>

#include "absl/base/log_severity.h"
#include "absl/log/log.h"
#include "sandboxed_api/sandbox2/logserver.pb.h"

namespace sandbox2 {

LogServer::LogServer(int fd) : comms_(fd) {}

void LogServer::Run() {
  LogMessage msg;
  while (comms_.RecvProtoBuf(&msg)) {
    absl::LogSeverity severity = absl::NormalizeLogSeverity(msg.severity());
    const char* fatal_string = "";
    if (severity == absl::LogSeverity::kFatal) {
      // We don't want to trigger an abort() in the executor for FATAL logs.
      severity = absl::LogSeverity::kError;
      fatal_string = " FATAL";
    }
    LOG(LEVEL(severity)).AtLocation(msg.path().c_str(), msg.line())
        << "(sandboxee " << msg.pid() << fatal_string << "): " << msg.message();
  }

  LOG(INFO) << "Receive failed, shutting down LogServer";
}

}  // namespace sandbox2
