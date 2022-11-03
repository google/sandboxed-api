// Copyright 2020 Google LLC
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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_DIAGNOSTICS_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_DIAGNOSTICS_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceLocation.h"

namespace sapi {

// Returns a new status with a payload that encodes the specified Clang source
// location.
absl::Status MakeStatusWithDiagnostic(clang::SourceLocation loc,
                                      absl::StatusCode code,
                                      absl::string_view message);

// Returns a new UNKNOWN status with a payload that encodes the specified Clang
// source location.
absl::Status MakeStatusWithDiagnostic(clang::SourceLocation loc,
                                      absl::string_view message);

// Extracts the Clang source location encoded in a status payload.
absl::optional<clang::SourceLocation> GetDiagnosticLocationFromStatus(
    const absl::Status& status);

clang::DiagnosticBuilder ReportWarning(clang::DiagnosticsEngine& de,
                                       clang::SourceLocation loc,
                                       absl::string_view message);

clang::DiagnosticBuilder ReportFatalError(clang::DiagnosticsEngine& de,
                                          clang::SourceLocation loc,
                                          absl::string_view message);

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_DIAGNOSTICS_H_
