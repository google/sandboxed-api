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
  Limits() {
    set_rlimit_as(kIniRLimAS);
    set_rlimit_cpu(kIniRLimCPU);
    set_rlimit_fsize(kIniRLimFSIZE);
    set_rlimit_nofile(kIniRLimNOFILE);
    set_rlimit_core(kIniRLimCORE);
    set_walltime_limit(absl::Seconds(kIniWallTimeLimit));
  }

  Limits(const Limits&) = delete;
  Limits& operator=(const Limits&) = delete;

  // Rlimit-s getters/setters.
  //
  // Use RLIM64_INFINITY for unlimited values, but remember that some of those
  // cannot exceed system limits (e.g. RLIMIT_NOFILE).
  const rlimit64& rlimit_as() const { return rlimit_as_; }
  Limits& set_rlimit_as(const rlimit64& value) {
    rlimit_as_ = value;
    return *this;
  }
  Limits& set_rlimit_as(uint64_t value) {
    rlimit_as_.rlim_cur = value;
    rlimit_as_.rlim_max = value;
    return *this;
  }

  const rlimit64& rlimit_cpu() const { return rlimit_cpu_; }
  Limits& set_rlimit_cpu(const rlimit64& value) {
    rlimit_cpu_ = value;
    return *this;
  }
  Limits& set_rlimit_cpu(uint64_t value) {
    rlimit_cpu_.rlim_cur = value;
    rlimit_cpu_.rlim_max = value;
    return *this;
  }

  const rlimit64& rlimit_fsize() const { return rlimit_fsize_; }
  Limits& set_rlimit_fsize(const rlimit64& value) {
    rlimit_fsize_ = value;
    return *this;
  }
  Limits& set_rlimit_fsize(uint64_t value) {
    rlimit_fsize_.rlim_cur = value;
    rlimit_fsize_.rlim_max = value;
    return *this;
  }

  const rlimit64& rlimit_nofile() const { return rlimit_nofile_; }
  Limits& set_rlimit_nofile(const rlimit64& value) {
    rlimit_nofile_ = value;
    return *this;
  }
  Limits& set_rlimit_nofile(uint64_t value) {
    rlimit_nofile_.rlim_cur = value;
    rlimit_nofile_.rlim_max = value;
    return *this;
  }

  const rlimit64& rlimit_core() const { return rlimit_core_; }
  Limits& set_rlimit_core(const rlimit64& value) {
    rlimit_core_ = value;
    return *this;
  }
  Limits& set_rlimit_core(uint64_t value) {
    rlimit_core_.rlim_cur = value;
    rlimit_core_.rlim_max = value;
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
  // Initial values for limits. Fields of rlimit64 are defined as __u64,
  // so we use uint64_t here.
  static constexpr uint64_t kIniRLimAS = RLIM64_INFINITY;
  // 1024 seconds of real CPU time for each sandboxed process.
  static constexpr uint64_t kIniRLimCPU = (1ULL << 10);
  // 8GiB - Maximum size of individual files that can be created by each
  // sandboxed process.
  static constexpr uint64_t kIniRLimFSIZE = (8ULL << 30);
  // 1024 file descriptors which can be used by each sandboxed process.
  static constexpr uint64_t kIniRLimNOFILE = (1ULL << 10);
  // No core files are allowed by default
  static constexpr uint64_t kIniRLimCORE = (0);
  // 120s - this is wall-time limit. Depending on the sandboxed load, this one,
  // or the RLIMIT_CPU limit might be triggered faster
  // cf. (https://en.wikipedia.org/wiki/Time_(Unix)#Real_time_vs_CPU_time)
  static constexpr time_t kIniWallTimeLimit = (120ULL);

  // Address space size of a process, if big enough (say, above 512M), it's a
  // crude representation of maximum RAM size used by the sandboxed process.
  rlimit64 rlimit_as_;
  // CPU time, might be triggered faster than the wall-time limit, if many
  // threads are used.
  rlimit64 rlimit_cpu_;
  // Number of bytes which can be written to the FS by the process (just
  // creating empty files is always allowed).
  rlimit64 rlimit_fsize_;
  // Number of NEW file descriptors which can be obtained by a process. 0
  // means that no new descriptors (files, sockets) can be created.
  rlimit64 rlimit_nofile_;
  // Size of a core file which is allowed to be created. Should be 0, unless
  // you know what you are doing.
  rlimit64 rlimit_core_;
  // Getter for the client_limits_ structure.

  // Wall-time limit (local to Monitor).
  absl::Duration wall_time_limit_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_LIMITS_H_
