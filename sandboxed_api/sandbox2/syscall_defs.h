#ifndef SANDBOXED_API_SANDBOX2_SYSCALL_DEFS_H_
#define SANDBOXED_API_SANDBOX2_SYSCALL_DEFS_H_

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/syscall.h"

namespace sandbox2 {
namespace syscalls {

constexpr int kMaxArgs = 6;

}  // namespace syscalls

class SyscallTable {
 public:
  struct Entry;

  // Returns the syscall table for the architecture.
  static SyscallTable get(sapi::cpu::Architecture arch);

  int size() { return data_.size(); }

  absl::string_view GetName(int syscall) const;

  std::vector<std::string> GetArgumentsDescription(int syscall,
                                                   const uint64_t values[],
                                                   pid_t pid) const;

 private:
  constexpr SyscallTable() = default;
  explicit constexpr SyscallTable(absl::Span<const Entry> data) : data_(data) {}

  const absl::Span<const Entry> data_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_SYSCALL_DEFS_H_
