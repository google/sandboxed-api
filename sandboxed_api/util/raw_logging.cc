// Copyright 2020 Google LLC. All Rights Reserved.
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

#include "sandboxed_api/util/raw_logging.h"

#include <cstdlib>
#include <limits>

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace sapi {
namespace internal {

bool VLogIsOn(int verbose_level) {
  static int external_verbose_level = [] {
    int external_verbose_level = std::numeric_limits<int>::max();
    char* env_var = getenv("SAPI_VLOG_LEVEL");
    if (!env_var) {
      return external_verbose_level;
    }
    ABSL_RAW_CHECK(absl::SimpleAtoi(env_var, &external_verbose_level) &&
                       external_verbose_level >= 0,
                   "SAPI_VLOG_LEVEL needs to be an integer >= 0");
    return external_verbose_level;
  }();
  return verbose_level >= external_verbose_level;
}

}  // namespace internal
}  // namespace sapi
