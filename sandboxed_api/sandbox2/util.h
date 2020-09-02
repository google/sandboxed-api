// Copyright 2019 Google LLC
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

// The sandbox2::util namespace provides various, uncategorized, functions
// useful for creating sandboxes.

#ifndef SANDBOXED_API_SANDBOX2_UTIL_H_
#define SANDBOXED_API_SANDBOX2_UTIL_H_

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/base/macros.h"
#include "absl/status/statusor.h"

namespace sandbox2 {
namespace util {

// Converts an array of char* (terminated by a nullptr, like argv, or environ
// arrays), to an std::vector<std::string>.
void CharPtrArrToVecString(char* const* arr, std::vector<std::string>* vec);

// Converts a vector of strings to a newly allocated array. The array is limited
// by the terminating nullptr entry (like environ or argv). It must be freed by
// the caller.
const char** VecStringToCharPtrArr(const std::vector<std::string>& vec);

// Returns the program name (via /proc/self/comm) for a given PID.
std::string GetProgName(pid_t pid);

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

// Reads a path string (NUL-terminated, shorter than PATH_MAX) from another
// process memory
absl::StatusOr<std::string> ReadCPathFromPid(pid_t pid, uintptr_t ptr);

}  // namespace util
}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_H_
