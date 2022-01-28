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

#ifndef SANDBOXED_API_SANDBOX2_UNWIND_UNWIND_H_
#define SANDBOXED_API_SANDBOX2_UNWIND_UNWIND_H_

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "sandboxed_api/sandbox2/comms.h"

namespace sandbox2 {

// Used to map from an address to a human-readable symbol.
using SymbolMap = std::map<uint64_t, std::string>;

// Returns the symbol at `addr`, possibly with an offset into said symbol.
std::string GetSymbolAt(const SymbolMap& addr_to_symbol, uint64_t addr);

// Loads and returns a symbol map for a process with the provided `pid`.
absl::StatusOr<SymbolMap> LoadSymbolsMap(pid_t pid);

// Runs libunwind and the symbolizer and sends the results via comms.
bool RunLibUnwindAndSymbolizer(Comms* comms);

absl::StatusOr<std::vector<std::string>> RunLibUnwindAndSymbolizer(
    pid_t pid, int max_frames);

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UNWIND_UNWIND_H_
