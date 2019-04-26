// Copyright 2019 Google LLC. All Rights Reserved.
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

#ifndef SANDBOXED_API_SANDBOX2_LOGSINK_H_
#define SANDBOXED_API_SANDBOX2_LOGSINK_H_

#include <glog/logging.h>

#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/comms.h"

namespace sandbox2 {

// The LogSink will register itself with the logging facilities and forward all
// log messages to the executor on a given file descriptor.
class LogSink : public google::LogSink {
 public:
  static constexpr char kLogFDName[] = "sb2_logsink";

  explicit LogSink(int fd);
  ~LogSink() override;

  LogSink(const LogSink&) = delete;
  LogSink& operator=(const LogSink&) = delete;

  void send(google::LogSeverity severity, const char* full_filename,
            const char* base_filename, int line, const struct tm* tm_time,
            const char* message, size_t message_len) override;

 private:
  Comms comms_;

  // Needed to make the LogSink thread safe.
  absl::Mutex lock_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_LOGSINK_H_
