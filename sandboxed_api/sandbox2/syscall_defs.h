#ifndef SANDBOXED_API_SANDBOX2_SYSCALL_DEFS_H_
#define SANDBOXED_API_SANDBOX2_SYSCALL_DEFS_H_

#include <sys/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {
namespace syscalls {

constexpr int kMaxArgs = 6;

enum ArgType {
  kPath,
  kString,
  kGen,
  kStruct,
  kPtr,
  kArray,
  kInt,
  kFlags,
  kResource,
  kPid,
  kSignal,
  kIpcResource,
  kSharedAddress,
  // Used for syscalls like ioctl or prctl which have an integer subcommand
  // argument.
  kSubcommand,
  kGid,
  kUid,
  // These kLenN types indicate that the argument is a length, and which
  // other argument it applies to. I.e., if parameter 3 is type kPollFdArray,
  // and parameter 4 has the length, then parameter 4 is of type kLen3.
  kLen0,
  kLen1,
  kLen2,
  kLen3,
  kLen4,
  kLen5,
  kAddressFamily,
  kGidArray,
  kPollFdArray,
  kSockaddr,
};

constexpr absl::string_view ArgTypeToString(ArgType type) {
  switch (type) {
    case kPath:
      return "path";
    case kString:
      return "string";
    case kGen:
      return "generic type";
    case kStruct:
      return "struct";
    case kPtr:
      return "pointer";
    case kArray:
      return "array";
    case kInt:
      return "int";
    case kFlags:
      return "flags";
    case kResource:
      return "resource";
    case kPid:
      return "pid";
    case kSignal:
      return "signal";
    case kIpcResource:
      return "ipc resource";
    case kSharedAddress:
      return "shared address";
    case kSubcommand:
      return "subcommand";
    case kGid:
      return "gid";
    case kUid:
      return "uid";
    case kLen0:
      return "length of parameter 0";
    case kLen1:
      return "length of parameter 1";
    case kLen2:
      return "length of parameter 2";
    case kLen3:
      return "length of parameter 3";
    case kLen4:
      return "length of parameter 4";
    case kLen5:
      return "length of parameter 5";
    case kAddressFamily:
      return "address family";
    case kGidArray:
      return "gid array";
    case kPollFdArray:
      return "poll fd array";
    case kSockaddr:
      return "sockaddr struct";
    default:
      return "invalid type";
  }
}

class ArgData {
 public:
  template <typename T>
  struct StructArray {
    std::vector<T> array;
    bool truncated;
  };

  ArgData(syscalls::ArgType type, pid_t pid, uint64_t value,
          std::optional<uint64_t> length = std::nullopt)
      : type_(type), pid_(pid), value_(value), length_(length) {}

  ArgType type() const { return type_; }
  pid_t pid() const { return pid_; }
  uint64_t value() const { return value_; }
  std::optional<uint64_t> length() const { return length_; }

  std::string GetDescription() const;

  absl::StatusOr<std::string> ReadAsString() const {
    return util::ReadCPathFromPid(pid_, value_);
  }

  template <typename T>
  absl::StatusOr<T> ReadAsStruct() const {
    if (length_.has_value() && *length_ < sizeof(T)) {
      return absl::InternalError(absl::StrFormat(
          "specified length [%llu] is not enough for to sizeof(%s) == %llu",
          *length_, typeid(T).name(), sizeof(T)));
    }
    SAPI_ASSIGN_OR_RETURN(std::vector<uint8_t> b,
                          util::ReadBytesFromPid(pid_, value_, sizeof(T)));
    return BytesToStruct<T>(b);
  }

  template <typename T>
  absl::StatusOr<StructArray<T>> ReadAsStructArray() const {
    static uint64_t kMaxAllowedBytes = 1 << 20;  // 1MB

    if (!length_.has_value()) {
      return absl::InternalError("length is not set");
    }

    bool truncated = false;
    uint64_t length = *length_ * sizeof(T);
    if (length > kMaxAllowedBytes) {
      truncated = true;
      length = (kMaxAllowedBytes / sizeof(T)) * sizeof(T);
    }

    SAPI_ASSIGN_OR_RETURN(std::vector<uint8_t> b,
                          util::ReadBytesFromPid(pid_, value_, length));
    absl::Span<const uint8_t> bytes = absl::MakeSpan(b);
    if (bytes.size() < length) {
      return absl::InternalError("could not read full struct array");
    }
    std::vector<T> ret;
    for (size_t i = 0; i < bytes.size(); i += sizeof(T)) {
      SAPI_ASSIGN_OR_RETURN(T t, BytesToStruct<T>(bytes.subspan(i, sizeof(T))));
      ret.push_back(t);
    }

    return StructArray<T>{std::move(ret), truncated};
  }

 private:
  template <typename T>
  static absl::StatusOr<T> BytesToStruct(absl::Span<const uint8_t> bytes) {
    static_assert(std::is_pod<T>(), "Can only cast bytes to POD structs");
    if (bytes.size() < sizeof(T)) {
      return absl::InternalError(absl::StrFormat(
          "bytes size [%llu] is not equal to sizeof(%s) == %llu", bytes.size(),
          typeid(T).name(), sizeof(T)));
    }
    T t;
    memcpy(&t, bytes.data(), sizeof(T));
    return t;
  }

  absl::StatusOr<std::string> GetDescriptionImpl() const;

  syscalls::ArgType type_;
  pid_t pid_;
  uint64_t value_;
  std::optional<uint64_t> length_;
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

  std::vector<syscalls::ArgData> GetArgumentsData(int syscall,
                                                  const uint64_t values[],
                                                  pid_t pid) const;

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
