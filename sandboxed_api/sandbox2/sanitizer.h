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

// The sandbox2::sanitizer namespace provides functions which bring a process
// into a state in which it can be safely sandboxed.

#ifndef SANDBOXED_API_SANDBOX2_SANITIZER_H_
#define SANDBOXED_API_SANDBOX2_SANITIZER_H_

#include <set>
#include <string>

#include "absl/base/macros.h"

namespace sandbox2 {
namespace sanitizer {

// Reads a list of open file descriptors in the current process.
bool GetListOfFDs(std::set<int>* fds);

// Closes all file descriptors in the current process except the ones in
// fd_exceptions.
bool CloseAllFDsExcept(const std::set<int>& fd_exceptions);

// Marks all file descriptors as close-on-exec, except the ones in
// fd_exceptions.
bool MarkAllFDsAsCOEExcept(const std::set<int>& fd_exceptions);

// Returns the number of threads in the process 'pid'. Returns -1 in case of
// errors.
int GetNumberOfThreads(int pid);

// When running under TSAN, it will spawn a background thread. This is not
// desirable for sandboxing purposes. We will notify its background thread
// that we wish for it to finish and then wait for it to be done. It is safe
// to call this function more than once, since it keeps track of whether it
// has already notified TSAN.
// This function does nothing if not running under TSAN.
void WaitForTsan();

// Sanitizes current process (which will not execve a sandboxed binary).
// File-descriptors in fd_exceptions will be either closed
// (close_fds == true), or marked as close-on-exec (close_fds == false).
bool SanitizeCurrentProcess(const std::set<int>& fd_exceptions, bool close_fds);

// Returns a list of tasks for a pid.
bool GetListOfTasks(int pid, std::set<int>* tasks);

}  // namespace sanitizer
}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_SANITIZER_H_
