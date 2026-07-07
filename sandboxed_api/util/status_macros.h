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

// This file used to be a custom fork of util/task/status_macros.h.

#ifndef THIRD_PARTY_SAPI_UTIL_STATUS_MACROS_H_
#define THIRD_PARTY_SAPI_UTIL_STATUS_MACROS_H_

// IWYU pragma: begin_exports
#include "absl/status/status_macros.h"  // INLINER_FORWARD_TO
// IWYU pragma: end_exports

#define SAPI_RETURN_IF_ERROR ABSL_RETURN_IF_ERROR
#define SAPI_ASSIGN_OR_RETURN ABSL_ASSIGN_OR_RETURN

#endif  // THIRD_PARTY_SAPI_UTIL_STATUS_MACROS_H_
