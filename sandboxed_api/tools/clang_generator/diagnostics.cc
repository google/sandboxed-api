// Copyright 2020 Google LLC
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

#include "sandboxed_api/tools/clang_generator/diagnostics.h"

#include "absl/strings/cord.h"

namespace sapi {

constexpr absl::string_view kSapiStatusPayload =
    "https://github.com/google/sandboxed-api";

absl::Status MakeStatusWithDiagnostic(clang::SourceLocation loc,
                                      absl::string_view message) {
  absl::Status status = absl::UnknownError(message);
  absl::Cord payload;
  uint64_t raw_loc = loc.getRawEncoding();
  payload.Append(
      absl::string_view(reinterpret_cast<char*>(&raw_loc), sizeof(raw_loc)));
  status.SetPayload(kSapiStatusPayload, std::move(payload));
  return status;
}

absl::optional<clang::SourceLocation> GetDiagnosticLocationFromStatus(
    const absl::Status& status) {
  if (auto payload =
          status.GetPayload(kSapiStatusPayload).value_or(absl::Cord());
      payload.size() == sizeof(uint64_t)) {
    return clang::SourceLocation::getFromRawEncoding(
        *reinterpret_cast<const uint64_t*>(payload.Flatten().data()));
  }
  return absl::nullopt;
}

clang::DiagnosticBuilder ReportFatalError(clang::DiagnosticsEngine& de,
                                          clang::SourceLocation loc,
                                          absl::string_view message) {
  clang::DiagnosticBuilder builder =
      de.Report(loc, de.getCustomDiagID(clang::DiagnosticsEngine::Fatal,
                                        "header generation failed: %0"));
  builder.AddString(llvm::StringRef(message.data(), message.size()));
  return builder;
}

}  // namespace sapi
