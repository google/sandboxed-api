#ifndef SANDBOXED_API_SANDBOX2_SYSCALL_DEFS_H_
#define SANDBOXED_API_SANDBOX2_SYSCALL_DEFS_H_

#include <sys/types.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/syscall.h"

namespace sandbox2 {
namespace syscalls {

constexpr int kMaxArgs = 6;

// Type of a given syscall argument. Used with argument conversion routines.
enum ArgType {
  kGen = 1,
  kInt,
  kPath,
  kHex,
  kOct,
  kSocketCall,
  kSocketCallPtr,
  kSignal,
  kString,
  kAddressFamily,
  kSockaddr,
  kSockmsghdr,
  kCloneFlag,
};

}  // namespace syscalls

class SyscallTable {
 public:
  // Single syscall definition
  struct Entry {
    // Returns the number of arguments which given syscall takes.
    int GetNumArgs() const {
      if (num_args < 0 || num_args > syscalls::kMaxArgs) {
        return syscalls::kMaxArgs;
      }
      return num_args;
    }

    static std::string GetArgumentDescription(uint64_t value,
                                              syscalls::ArgType type,
                                              pid_t pid);

    static constexpr bool BySyscallNr(const SyscallTable::Entry& a,
                                      const SyscallTable::Entry& b) {
      return a.nr < b.nr;
    }

    int nr;
    absl::string_view name;
    int num_args;
    std::array<syscalls::ArgType, syscalls::kMaxArgs> arg_types;
  };

  // Returns the syscall table for the architecture.
  static SyscallTable get(sapi::cpu::Architecture arch);

  int size() { return data_.size(); }

  absl::string_view GetName(int syscall) const;

  std::vector<std::string> GetArgumentsDescription(int syscall,
                                                   const uint64_t values[],
                                                   pid_t pid) const;

  absl::StatusOr<Entry> GetEntry(int syscall) const;
  // Returns the first entry matching the provided name.
  absl::StatusOr<Entry> GetEntry(absl::string_view name) const;

  absl::Span<const Entry> GetEntries() const { return data_; }

 private:
  constexpr SyscallTable() = default;
  explicit constexpr SyscallTable(absl::Span<const Entry> data) : data_(data) {}

  const absl::Span<const Entry> data_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_SYSCALL_DEFS_H_
