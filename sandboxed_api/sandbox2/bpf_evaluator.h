// Copyright 2024 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_BPF_EVALUATOR_H_
#define SANDBOXED_API_SANDBOX2_BPF_EVALUATOR_H_

#include <linux/filter.h>
#include <linux/seccomp.h>

#include <cstdint>

#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace sandbox2::bpf {

// Evaluates a BPF program against a given seccomp_data.
//
// Returns the return value of the BPF program.
absl::StatusOr<uint32_t> Evaluate(absl::Span<const sock_filter> prog,
                                  const struct seccomp_data& data);

}  // namespace sandbox2::bpf

#endif  // SANDBOXED_API_SANDBOX2_BPF_EVALUATOR_H_
