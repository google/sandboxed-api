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

#ifndef SANDBOXED_API_UTIL_RAW_LOGGING_H_
#define SANDBOXED_API_UTIL_RAW_LOGGING_H_

#include "sandboxed_api/util/raw_logging.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "sandboxed_api/sandbox2/util/strerror.h"

namespace sapi {
namespace internal {

bool VLogIsOn(int verbose_level);

}  // namespace internal
}  // namespace sapi

// Returns whether SAPI verbose logging is enabled, as determined by the
// SAPI_VLOG_LEVEL environment variable.
#define SAPI_VLOG_IS_ON(verbose_level) ::sapi::internal::VLogIsOn(verbose_level)

// This is similar to LOG(severity) << format..., but intended for low-level
// modules that cannot use regular logging (i.e. in static initializers). It
// also uses printf-style formatting using absl::StrFormat for type-safety.
#define SAPI_RAW_LOG(severity, format, ...) \
  ABSL_INTERNAL_LOG(severity, absl::StrFormat((format), ##__VA_ARGS__))

// Like SAPI_RAW_LOG(), but also logs the current value of errno and its
// corresponding error message.
#define SAPI_RAW_PLOG(severity, format, ...)                                 \
  ABSL_INTERNAL_LOG(                                                         \
      severity, absl::StrCat(absl::StrFormat((format), ##__VA_ARGS__), ": ", \
                             ::sandbox2::StrError(errno), " [", errno, "]"))

// If verbose logging is enabled, uses SAPI_RAW_LOG() to log.
#define SAPI_RAW_VLOG(verbose_level, format, ...) \
  if (sapi::internal::VLogIsOn(verbose_level)) {  \
    SAPI_RAW_LOG(INFO, (format), ##__VA_ARGS__);  \
  }

// Like regular CHECK(), but uses raw logging and printf-style formatting.
#define SAPI_RAW_CHECK(condition, format, ...) \
  ABSL_INTERNAL_CHECK((condition), absl::StrFormat((format), ##__VA_ARGS__))

// Like SAPI_RAW_CHECK(), but also logs errno and a message (similar to
// SAPI_RAW_PLOG()).
#define SAPI_RAW_PCHECK(condition, format, ...)                    \
  ABSL_INTERNAL_CHECK(                                             \
      (condition),                                                 \
      absl::StrCat(absl::StrFormat((format), ##__VA_ARGS__), ": ", \
                   ::sandbox2::StrError(errno), " [", errno, "]"))

#endif  // SANDBOXED_API_UTIL_RAW_LOGGING_H_
