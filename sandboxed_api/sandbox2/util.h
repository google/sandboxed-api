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

// The sandbox2::util namespace provides various, uncategorized, functions
// useful for creating sandboxes.

#ifndef SANDBOXED_API_SANDBOX2_UTIL_H_
#define SANDBOXED_API_SANDBOX2_UTIL_H_

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/status/statusor.h"

namespace sandbox2::util {

// Converts an array of char* (terminated by a nullptr, like argv, or environ
// arrays), to an std::vector<std::string>.
ABSL_DEPRECATED("Use CharPtrArray(arr).ToStringVector() instead")
void CharPtrArrToVecString(char* const* arr, std::vector<std::string>* vec);

// Converts a vector of strings to a newly allocated array. The array is limited
// by the terminating nullptr entry (like environ or argv). It must be freed by
// the caller.
ABSL_DEPRECATED("Use CharPtrArray class instead")
const char** VecStringToCharPtrArr(const std::vector<std::string>& vec);

// An char ptr array limited by the terminating nullptr entry (like environ
// or argv).
class CharPtrArray {
 public:
  CharPtrArray(char* const* array);
  static CharPtrArray FromStringVector(const std::vector<std::string>& vec);

  const std::vector<const char*>& array() const { return array_; }

  const char* const* data() const { return array_.data(); }

  std::vector<std::string> ToStringVector() const;

 private:
  CharPtrArray(const std::vector<std::string>& vec);

  const std::string content_;
  std::vector<const char*> array_;
};

// Returns the program name (via /proc/self/comm) for a given PID.
std::string GetProgName(pid_t pid);

// Returns the command line (via /proc/self/cmdline) for a given PID. The
// argument separators '\0' are converted to spaces.
std::string GetCmdLine(pid_t pid);

// Returns the specified line from /proc/<pid>/status for a given PID. 'value'
// is a field name like "Threads" or "Tgid".
std::string GetProcStatusLine(int pid, const std::string& value);

// Invokes a syscall, avoiding on-stack argument promotion, as it might happen
// with vararg syscall() function.
long Syscall(long sys_no,  // NOLINT
             uintptr_t a1 = 0, uintptr_t a2 = 0, uintptr_t a3 = 0,
             uintptr_t a4 = 0, uintptr_t a5 = 0, uintptr_t a6 = 0);

// Recursively creates a directory, skipping segments that already exist.
bool CreateDirRecursive(const std::string& path, mode_t mode);

// Fork based on clone() which updates glibc's PID/TID caches - Based on:
// https://chromium.googlesource.com/chromium/src/+/9eb564175dbd452196f782da2b28e3e8e79c49a5%5E!/
//
// Return values as for 'man 2 fork'.
pid_t ForkWithFlags(int flags);

// Creates a new memfd.
bool CreateMemFd(int* fd, const char* name = "buffer_file");

// Executes a the program given by argv and the specified environment and
// captures any output to stdout/stderr.
absl::StatusOr<int> Communicate(const std::vector<std::string>& argv,
                                const std::vector<std::string>& envv,
                                std::string* output);

// Returns signal description.
std::string GetSignalName(int signo);

// Returns rlimit resource name
std::string GetRlimitName(int resource);

// Returns ptrace event name
std::string GetPtraceEventName(int event);

// Reads a path string (NUL-terminated, shorter than PATH_MAX) from another
// process memory
absl::StatusOr<std::string> ReadCPathFromPid(pid_t pid, uintptr_t ptr);

}  // namespace sandbox2::util

#endif  // SANDBOXED_API_SANDBOX2_UTIL_H_
