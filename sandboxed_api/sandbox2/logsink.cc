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

#include "sandboxed_api/sandbox2/logsink.h"

#include <unistd.h>

#include <csignal>
#include <iostream>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/logserver.pb.h"

namespace sandbox2 {

constexpr char LogSink::kLogFDName[];

LogSink::LogSink(int fd) : comms_(fd) { AddLogSink(this); }

LogSink::~LogSink() { RemoveLogSink(this); }

void LogSink::send(google::LogSeverity severity, const char* full_filename,
                   const char* base_filename, int line,
                   const struct tm* tm_time, const char* message,
                   size_t message_len) {
  absl::MutexLock l(&lock_);

  LogMessage msg;
  msg.set_severity(static_cast<int>(severity));
  msg.set_path(base_filename);
  msg.set_line(line);
  msg.set_message(absl::StrCat(absl::string_view{message, message_len}, "\n"));
  msg.set_pid(getpid());

  if (!comms_.SendProtoBuf(msg)) {
    std::cerr << "sending log message to supervisor failed: " << std::endl
              << msg.DebugString() << std::endl;
  }

  if (severity == google::FATAL) {
    // Raise a SIGABRT to prevent the remaining code in logging to try to dump a
    // symbolized stack trace which can lead to syscall violations.
    kill(0, SIGABRT);
  }
}

}  // namespace sandbox2
