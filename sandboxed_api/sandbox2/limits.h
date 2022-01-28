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

// The sandbox2::Limits class defined various client- and sandbox- side limits
// which are applied to the execution process of sandboxees.

#ifndef SANDBOXED_API_SANDBOX2_LIMITS_H_
#define SANDBOXED_API_SANDBOX2_LIMITS_H_

#include <sys/resource.h>

#include <cstdint>
#include <ctime>

#include "absl/base/macros.h"
#include "absl/time/time.h"

namespace sandbox2 {

class Limits final {
 public:
  Limits() = default;

  Limits(const Limits&) = delete;
  Limits& operator=(const Limits&) = delete;

  // rlimits getters/setters.
  //
  // Use RLIM64_INFINITY for unlimited values, but remember that some of those
  // cannot exceed system limits (e.g. RLIMIT_NOFILE).
  const rlimit64& rlimit_as() const { return rlimit_as_; }
  Limits& set_rlimit_as(const rlimit64& value) {
    rlimit_as_ = value;
    return *this;
  }
  Limits& set_rlimit_as(uint64_t value) {
    rlimit_as_ = MakeRlimit64(value);
    return *this;
  }

  const rlimit64& rlimit_cpu() const { return rlimit_cpu_; }
  Limits& set_rlimit_cpu(const rlimit64& value) {
    rlimit_cpu_ = value;
    return *this;
  }
  Limits& set_rlimit_cpu(uint64_t value) {
    rlimit_cpu_ = MakeRlimit64(value);
    return *this;
  }

  const rlimit64& rlimit_fsize() const { return rlimit_fsize_; }
  Limits& set_rlimit_fsize(const rlimit64& value) {
    rlimit_fsize_ = value;
    return *this;
  }
  Limits& set_rlimit_fsize(uint64_t value) {
    rlimit_fsize_ = MakeRlimit64(value);
    return *this;
  }

  const rlimit64& rlimit_nofile() const { return rlimit_nofile_; }
  Limits& set_rlimit_nofile(const rlimit64& value) {
    rlimit_nofile_ = value;
    return *this;
  }
  Limits& set_rlimit_nofile(uint64_t value) {
    rlimit_nofile_ = MakeRlimit64(value);
    return *this;
  }

  const rlimit64& rlimit_core() const { return rlimit_core_; }
  Limits& set_rlimit_core(const rlimit64& value) {
    rlimit_core_ = value;
    return *this;
  }
  Limits& set_rlimit_core(uint64_t value) {
    rlimit_core_ = MakeRlimit64(value);
    return *this;
  }

  // Sets a wall time limit on an executor before running it. Set to
  // absl::ZeroDuration() to disarm.  The walltime limit is a timeout duration
  // (e.g. 10 secs) not a deadline (e.g. 12:00). This can be useful in a simple
  // scenario to set a wall limit before running the sandboxee, run the
  // sandboxee, and expect it to finish within the limit. For an example, see
  // examples/crc4.
  Limits& set_walltime_limit(absl::Duration value) {
    wall_time_limit_ = value;
    return *this;
  }
  absl::Duration wall_time_limit() const { return wall_time_limit_; }

 private:
  constexpr rlimit64 MakeRlimit64(uint64_t value) {
    return {.rlim_cur = value, .rlim_max = value};
  }

  // Address space size of a process, if big enough (say, above 512M), this
  // will be a rough approximation of the maximum RAM usage by the sandboxed
  // process.
  rlimit64 rlimit_as_ = MakeRlimit64(RLIM64_INFINITY);

  // CPU time, measured in seconds. This limit might be triggered faster than
  // the wall-time limit, if many threads are used.
  rlimit64 rlimit_cpu_ = MakeRlimit64(1024 /* seconds */);

  // Total number of bytes that can be written to the filesystem by the process
  // (creating empty files is not considered writing).
  rlimit64 rlimit_fsize_ = MakeRlimit64(8ULL << 30 /* 8GiB */);

  // Number of NEW file descriptors which can be obtained by a process. 0
  // means that no new descriptors (files, sockets) can be created.
  rlimit64 rlimit_nofile_ = MakeRlimit64(1024);

  // Size of a core file which is allowed to be created. The default value of
  // zero disables the creation of core files. Unless you have special
  // requirements, this should not be changed.
  rlimit64 rlimit_core_ = MakeRlimit64(0);

  // Wall-time limit (local to Monitor). Depending on the sandboxed load, this
  // one, or RLIMIT_CPU limit might be triggered faster (see
  // https://en.wikipedia.org/wiki/Time_(Unix)#Real_time_vs_CPU_time).
  absl::Duration wall_time_limit_ = absl::Seconds(120);
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_LIMITS_H_
