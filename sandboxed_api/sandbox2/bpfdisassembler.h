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

#ifndef SANDBOXED_API_SANDBOX2_BPFDISASSEMBLER_H_
#define SANDBOXED_API_SANDBOX2_BPFDISASSEMBLER_H_

#include <string>
#include <vector>

#include "absl/types/span.h"

struct sock_filter;

namespace sandbox2 {
namespace bpf {

// Decodes a BPF instruction into textual representation.
std::string DecodeInstruction(const sock_filter& inst, int pc);

// Disassembles a BPF program.
// Returns a human-readable textual represenation.
std::string Disasm(absl::Span<const sock_filter> prog);

}  // namespace bpf
}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_BPFDISASSEMBLER_H_
