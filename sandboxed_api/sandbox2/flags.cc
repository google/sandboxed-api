#include "sandboxed_api/sandbox2/flags.h"

#include <csignal>  // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

// sandbox2:global_forkserver
ABSL_FLAG(std::string, sandbox2_forkserver_binary_path, "",
          "Path to forkserver_bin binary");
ABSL_FLAG(sandbox2::GlobalForkserverStartModeSet,
          sandbox2_forkserver_start_mode,
          sandbox2::GlobalForkserverStartModeSet(
              sandbox2::GlobalForkserverStartMode::kOnDemand)
          ,
          "When Sandbox2 Forkserver process should be started");

// sandbox2:monitor_base
ABSL_FLAG(bool, sandbox2_report_on_sandboxee_signal, true,
          "Report sandbox2 sandboxee deaths caused by signals");
ABSL_FLAG(bool, sandbox2_report_on_sandboxee_timeout, true,
          "Report sandbox2 sandboxee timeouts");

// sandbox2:monitor_ptrace
ABSL_FLAG(bool, sandbox2_log_all_stack_traces, false,
          "If set, sandbox2 monitor will log stack traces of all monitored "
          "threads/processes that are reported to terminate with a signal.");
ABSL_FLAG(bool, sandbox2_monitor_ptrace_use_deadline_manager,
          false,
          "If set, ptrace monitor will use deadline manager to enforce "
          "deadlines and as notification mechanism.");
ABSL_FLAG(bool, sandbox2_log_unobtainable_stack_traces_errors, true,
          "If set, unobtainable stack trace will be logged as errors.");
ABSL_FLAG(absl::Duration, sandbox2_stack_traces_collection_timeout,
          absl::Seconds(1),
          "How much time should be spent on logging threads' stack traces on "
          "monitor shut down. Only relevent when collection of all stack "
          "traces is enabled.");

// sandbox2:policy
ABSL_FLAG(bool, sandbox2_danger_danger_permit_all, false,
          "Allow all syscalls, useful for testing");
ABSL_FLAG(std::string, sandbox2_danger_danger_permit_all_and_log, "",
          "Allow all syscalls and log them into specified file");

// sandbox2:stack_trace
ABSL_FLAG(bool, sandbox_disable_all_stack_traces, false,
          "Completely disable stack trace collection for sandboxees");
ABSL_RETIRED_FLAG(bool, sandbox_libunwind_crash_handler, true,
                  "Sandbox libunwind when handling violations (preferred)");

// sandbox2/util:deadline_manager
ABSL_FLAG(int, sandbox2_deadline_manager_signal, SIGRTMAX - 1,
          "Signal to use for deadline notifications - must be not otherwise "
          "used by the process (default: SIGRTMAX - 1)");

namespace sandbox2 {

bool AbslParseFlag(absl::string_view text, GlobalForkserverStartModeSet* out,
                   std::string* error) {
  *out = {};
  if (text == "never") {
    return true;
  }
  for (absl::string_view mode : absl::StrSplit(text, ',')) {
    mode = absl::StripAsciiWhitespace(mode);
    if (mode == "ondemand") {
      *out |= GlobalForkserverStartMode::kOnDemand;
    } else {
      *error = absl::StrCat("Invalid forkserver start mode: ", mode);
      return false;
    }
  }
  return true;
}

std::string AbslUnparseFlag(GlobalForkserverStartModeSet in) {
  std::vector<std::string> str_modes;
  for (size_t i = 0; i < GlobalForkserverStartModeSet::kSize; ++i) {
    auto mode = static_cast<GlobalForkserverStartMode>(i);
    if (in.contains(mode)) {
      str_modes.emplace_back(ToString(mode));
    }
  }
  if (str_modes.empty()) {
    return "never";
  }
  return absl::StrJoin(str_modes, ",");
}

}  // namespace sandbox2
