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
