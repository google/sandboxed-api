#ifndef SANDBOXED_API_SANDBOX2_SYSCALL_DEFS_H_
#define SANDBOXED_API_SANDBOX2_SYSCALL_DEFS_H_

#include <sys/types.h>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/types/span.h"

namespace sandbox2 {

namespace syscalls {
constexpr int kMaxArgs = 6;
}

class SyscallTable {
 public:
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

  // Single syscall definition
  struct Entry {
    const char* const name;
    const int num_args;
    const ArgType arg_types[syscalls::kMaxArgs];

    // Returns the number of arguments which given syscall takes.
    int GetNumArgs() const {
      if (num_args < 0 || num_args > syscalls::kMaxArgs) {
        return syscalls::kMaxArgs;
      }
      return num_args;
    }

    std::vector<std::string> GetArgumentsDescription(
        const uint64_t values[syscalls::kMaxArgs], pid_t pid) const;
  };

#if defined(__x86_64__)
  static const absl::Span<const Entry> kSyscallDataX8664;
  static const absl::Span<const Entry> kSyscallDataX8632;
#elif defined(__powerpc64__)
  static const absl::Span<const Entry> kSyscallDataPPC64;
#endif

  constexpr SyscallTable() = default;
  constexpr SyscallTable(absl::Span<const Entry> data) : data_(data) {}

  int size() { return data_.size(); }
  const Entry& GetEntry(uint64_t syscall) const {
    static Entry invalid_entry{
        nullptr, syscalls::kMaxArgs, {kGen, kGen, kGen, kGen, kGen, kGen}};
    if (syscall < data_.size()) {
      return data_[syscall];
    }
    return invalid_entry;
  }

 private:
  const absl::Span<const Entry> data_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_SYSCALL_DEFS_H_
