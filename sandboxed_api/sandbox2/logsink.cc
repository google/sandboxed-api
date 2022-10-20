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
#include <string>

#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "sandboxed_api/sandbox2/logserver.pb.h"

namespace sandbox2 {

constexpr char LogSink::kLogFDName[];

LogSink::LogSink(int fd) : comms_(fd) { absl::AddLogSink(this); }

LogSink::~LogSink() { absl::RemoveLogSink(this); }

void LogSink::Send(const absl::LogEntry& e) {
  absl::MutexLock l(&lock_);

  LogMessage msg;
  msg.set_severity(static_cast<int>(e.log_severity()));
  msg.set_path(std::string(e.source_basename()));
  msg.set_line(e.source_line());
  msg.set_message(absl::StrCat(e.text_message(), "\n"));
  msg.set_pid(getpid());

  if (!comms_.SendProtoBuf(msg)) {
    absl::FPrintF(stderr, "sending log message to supervisor failed:\n%s\n",
                  e.text_message_with_prefix());
  }

  if (e.log_severity() == absl::LogSeverity::kFatal) {
    // Raise a SIGABRT to prevent the remaining code in logging to try to dump a
    // symbolized stack trace which can lead to syscall violations.
    kill(0, SIGABRT);
  }
}

}  // namespace sandbox2
