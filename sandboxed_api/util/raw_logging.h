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

// SAPI raw logging. Forked from Abseil's version.

#ifndef SANDBOXED_API_UTIL_RAW_LOGGING_H_
#define SANDBOXED_API_UTIL_RAW_LOGGING_H_

#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/log_severity.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/base/port.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "sandboxed_api/util/strerror.h"

// Exclude ABSL_RAW_LOG when running on Android because it will not be visible
// in logcat since Android sends anything written to stdout and stderr to
// /dev/null.
#if defined(ABSL_RAW_LOG) && !(__ANDROID__)
#define SAPI_RAW_LOG ABSL_RAW_LOG
#else
// This is similar to LOG(severity) << format..., but
// * it is to be used ONLY by low-level modules that can't use normal LOG()
// * it is designed to be a low-level logger that does not allocate any
//   memory and does not need any locks, hence:
// * it logs straight and ONLY to STDERR w/o buffering
// * it uses an explicit printf-format and arguments list
// * it will silently chop off really long message strings
// Usage example:
//   SAPI_RAW_LOG(ERROR, "Failed foo with %i: %s", status, error);
// This will print an almost standard log line like this to stderr only:
//   E0821 211317 file.cc:123] RAW: Failed foo with 22: bad_file

#define SAPI_RAW_LOG(severity, ...)                                            \
  do {                                                                         \
    constexpr const char* absl_raw_logging_internal_basename =                 \
        ::sapi::raw_logging_internal::Basename(__FILE__,                       \
                                               sizeof(__FILE__) - 1);          \
    ::sapi::raw_logging_internal::RawLog(SAPI_RAW_LOGGING_INTERNAL_##severity, \
                                         absl_raw_logging_internal_basename,   \
                                         __LINE__, __VA_ARGS__);               \
  } while (0)
#endif

#ifdef ABSL_RAW_CHECK
#define SAPI_RAW_CHECK ABSL_RAW_CHECK
#else
// Similar to CHECK(condition) << message, but for low-level modules:
// we use only SAPI_RAW_LOG that does not allocate memory.
// We do not want to provide args list here to encourage this usage:
//   if (!cond)  SAPI_RAW_LOG(FATAL, "foo ...", hard_to_compute_args);
// so that the args are not computed when not needed.
#define SAPI_RAW_CHECK(condition, message)                             \
  do {                                                                 \
    if (ABSL_PREDICT_FALSE(!(condition))) {                            \
      SAPI_RAW_LOG(FATAL, "Check %s failed: %s", #condition, message); \
    }                                                                  \
  } while (0)
#endif

#define SAPI_RAW_LOGGING_INTERNAL_INFO ::absl::LogSeverity::kInfo
#define SAPI_RAW_LOGGING_INTERNAL_WARNING ::absl::LogSeverity::kWarning
#define SAPI_RAW_LOGGING_INTERNAL_ERROR ::absl::LogSeverity::kError
#define SAPI_RAW_LOGGING_INTERNAL_FATAL ::absl::LogSeverity::kFatal

// Returns whether SAPI verbose logging is enabled, as determined by the
// SAPI_VLOG_LEVEL environment variable.
#define SAPI_VLOG_IS_ON(verbose_level) \
  ::sapi::raw_logging_internal::VLogIsOn(verbose_level)

#ifndef VLOG
// `VLOG` uses numeric levels to provide verbose logging that can configured at
// runtime, globally. `VLOG` statements are logged at `INFO` severity if they
// are logged at all; the numeric levels are on a different scale than the
// proper severity levels. Positive levels are disabled by default. Negative
// levels should not be used.
#define VLOG(verbose_level)                                                \
  for (int sapi_logging_internal_verbose_level = (verbose_level),          \
           sapi_logging_internal_log_loop = 1;                             \
       sapi_logging_internal_log_loop; sapi_logging_internal_log_loop = 0) \
  LOG_IF(INFO, SAPI_VLOG_IS_ON(sapi_logging_internal_verbose_level))       \
      .WithVerbosity(sapi_logging_internal_verbose_level)
#endif

// Like SAPI_RAW_LOG(), but also logs the current value of errno and its
// corresponding error message.
#define SAPI_RAW_PLOG(severity, format, ...)                              \
  do {                                                                    \
    char sapi_raw_plog_errno_buffer[100];                                 \
    const char* sapi_raw_plog_errno_str =                                 \
        ::sapi::RawStrError(errno, sapi_raw_plog_errno_buffer,            \
                            sizeof(sapi_raw_plog_errno_buffer));          \
    char sapi_raw_plog_buffer[::sapi::raw_logging_internal::kLogBufSize]; \
    absl::SNPrintF(sapi_raw_plog_buffer, sizeof(sapi_raw_plog_buffer),    \
                   (format), ##__VA_ARGS__);                              \
    SAPI_RAW_LOG(severity, "%s: %s [%d]", sapi_raw_plog_buffer,           \
                 sapi_raw_plog_errno_str, errno);                         \
  } while (0)

// If verbose logging is enabled, uses SAPI_RAW_LOG() to log.
#define SAPI_RAW_VLOG(verbose_level, format, ...)            \
  if (sapi::raw_logging_internal::VLogIsOn(verbose_level)) { \
    SAPI_RAW_LOG(INFO, (format), ##__VA_ARGS__);             \
  }

// Like SAPI_RAW_CHECK(), but also logs errno and a message (similar to
// SAPI_RAW_PLOG()).
#define SAPI_RAW_PCHECK(condition, format, ...)                             \
  do {                                                                      \
    if (ABSL_PREDICT_FALSE(!(condition))) {                                 \
      char sapi_raw_plog_errno_buffer[100];                                 \
      const char* sapi_raw_plog_errno_str =                                 \
          ::sapi::RawStrError(errno, sapi_raw_plog_errno_buffer,            \
                              sizeof(sapi_raw_plog_errno_buffer));          \
      char sapi_raw_plog_buffer[::sapi::raw_logging_internal::kLogBufSize]; \
      absl::SNPrintF(sapi_raw_plog_buffer, sizeof(sapi_raw_plog_buffer),    \
                     (format), ##__VA_ARGS__);                              \
      SAPI_RAW_LOG(FATAL, "Check %s failed: %s: %s [%d]", #condition,       \
                   sapi_raw_plog_buffer, sapi_raw_plog_errno_str, errno);   \
    }                                                                       \
  } while (0)

namespace sapi::raw_logging_internal {

constexpr int kLogBufSize = 3000;

// Helper function to implement ABSL_RAW_LOG
// Logs format... at "severity" level, reporting it
// as called from file:line.
// This does not allocate memory or acquire locks.
void RawLog(absl::LogSeverity severity, const char* file, int line,
            const char* format, ...) ABSL_PRINTF_ATTRIBUTE(4, 5);

// Writes the provided buffer directly to stderr, in a safe, low-level manner.
//
// In POSIX this means calling write(), which is async-signal safe and does
// not malloc.  If the platform supports the SYS_write syscall, we invoke that
// directly to side-step any libc interception.
void SafeWriteToStderr(const char* s, size_t len);

// compile-time function to get the "base" filename, that is, the part of
// a filename after the last "/" or "\" path separator.  The search starts at
// the end of the string; the second parameter is the length of the string.
constexpr const char* Basename(const char* fname, int offset) {
  return offset == 0 || fname[offset - 1] == '/' || fname[offset - 1] == '\\'
             ? fname + offset
             : Basename(fname, offset - 1);
}

bool VLogIsOn(int verbose_level);

}  // namespace sapi::raw_logging_internal

#endif  // SANDBOXED_API_UTIL_RAW_LOGGING_H_
