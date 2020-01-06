// Copyright 2020 Google LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sandboxed_api/sandbox2/logserver.h"

#include <string>

#include <glog/logging.h>
#include "sandboxed_api/sandbox2/logserver.pb.h"

namespace sandbox2 {

LogServer::LogServer(int fd) : comms_(fd) {}

void LogServer::Run() {
  namespace logging = ::google;
  LogMessage msg;
  while (comms_.RecvProtoBuf(&msg)) {
    logging::LogSeverity severity = msg.severity();
    const char* fatal_string = "";
    if (severity == logging::FATAL) {
      // We don't want to trigger an abort() in the executor for FATAL logs.
      severity = logging::ERROR;
      fatal_string = " FATAL";
    }
    logging::LogMessage log_message(msg.path().c_str(), msg.line(), severity);
    log_message.stream()
        << "(sandboxee " << msg.pid() << fatal_string << "): " << msg.message();
  }

  LOG(INFO) << "Receive failed, shutting down LogServer";
}

}  // namespace sandbox2
