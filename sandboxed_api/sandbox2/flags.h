#ifndef SANDBOXED_API_SANDBOX2_FLAGS_H_
#define SANDBOXED_API_SANDBOX2_FLAGS_H_

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/flags/declare.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

namespace sandbox2 {

enum class GlobalForkserverStartMode {
  kOnDemand,
  // MUST be the last element
  kNumGlobalForkserverStartModes,
};

constexpr absl::string_view ToString(GlobalForkserverStartMode mode) {
  switch (mode) {
    case GlobalForkserverStartMode::kOnDemand:
      return "ondemand";
    default:
      return "unknown";
  }
}

class GlobalForkserverStartModeSet {
 public:
  static constexpr size_t kSize = static_cast<size_t>(
      GlobalForkserverStartMode::kNumGlobalForkserverStartModes);

  GlobalForkserverStartModeSet() {}
  explicit GlobalForkserverStartModeSet(GlobalForkserverStartMode value) {
    value_[static_cast<size_t>(value)] = true;
  }
  GlobalForkserverStartModeSet& operator|=(GlobalForkserverStartMode value) {
    value_[static_cast<size_t>(value)] = true;
    return *this;
  }
  GlobalForkserverStartModeSet operator|(
      GlobalForkserverStartMode value) const {
    GlobalForkserverStartModeSet rv(*this);
    rv |= value;
    return rv;
  }
  bool contains(GlobalForkserverStartMode value) const {
    return value_[static_cast<size_t>(value)];
  }
  bool empty() { return value_.none(); }

 private:
  std::bitset<kSize> value_;
};

bool AbslParseFlag(absl::string_view text, GlobalForkserverStartModeSet* out,
                   std::string* error);
std::string AbslUnparseFlag(GlobalForkserverStartModeSet in);

}  // namespace sandbox2

// sandbox2:monitor_base
ABSL_DECLARE_FLAG(bool, sandbox2_report_on_sandboxee_signal);
ABSL_DECLARE_FLAG(bool, sandbox2_report_on_sandboxee_timeout);

// sandbox2:monitor_ptrace
ABSL_DECLARE_FLAG(bool, sandbox2_log_all_stack_traces);
ABSL_DECLARE_FLAG(bool, sandbox2_monitor_ptrace_use_deadline_manager);
ABSL_DECLARE_FLAG(bool, sandbox2_log_unobtainable_stack_traces_errors);
ABSL_DECLARE_FLAG(absl::Duration, sandbox2_stack_traces_collection_timeout);
ABSL_DECLARE_FLAG(absl::Duration,
                  sandbox2_monitor_ptrace_graceful_kill_timeout);

// sandbox2:global_forkserver
ABSL_DECLARE_FLAG(std::string, sandbox2_forkserver_binary_path);
ABSL_DECLARE_FLAG(sandbox2::GlobalForkserverStartModeSet,
                  sandbox2_forkserver_start_mode);

// sandbox2:policy
ABSL_DECLARE_FLAG(bool, sandbox2_danger_danger_permit_all);
ABSL_DECLARE_FLAG(std::string, sandbox2_danger_danger_permit_all_and_log);

// sandbox2:stack_trace
ABSL_DECLARE_FLAG(bool, sandbox_disable_all_stack_traces);
ABSL_DECLARE_FLAG(bool, sandbox_libunwind_crash_handler);  // retired flag

// sandbox2/util:deadline_manager
ABSL_DECLARE_FLAG(int, sandbox2_deadline_manager_signal);

#endif  // SANDBOXED_API_SANDBOX2_FLAGS_H_
