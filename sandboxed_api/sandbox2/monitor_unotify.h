#ifndef SANDBOXED_API_SANDBOX2_MONITOR_UNOTIFY_H_
#define SANDBOXED_API_SANDBOX2_MONITOR_UNOTIFY_H_

#include <linux/seccomp.h>
#include <sys/sysinfo.h>
#include <sys/types.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <thread>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/monitor_base.h"
#include "sandboxed_api/sandbox2/notify.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {

#ifndef SECCOMP_IOCTL_NOTIF_RECV
struct seccomp_notif {
  __u64 id;
  __u32 pid;
  __u32 flags;
  struct seccomp_data data;
};
#endif

class UnotifyMonitor : public MonitorBase {
 public:
  UnotifyMonitor(Executor* executor, Policy* policy, Notify* notify);
  ~UnotifyMonitor() { Join(); }

  void Kill() override {
    external_kill_request_flag_.clear(std::memory_order_relaxed);
    NotifyMonitor();
  }

  void DumpStackTrace() override {
    dump_stack_request_flag_.clear(std::memory_order_relaxed);
    NotifyMonitor();
  }

  void SetWallTimeLimit(absl::Duration limit) override {
    if (limit == absl::ZeroDuration()) {
      VLOG(1) << "Disarming walltime timer to ";
      deadline_millis_.store(0, std::memory_order_relaxed);
    } else {
      VLOG(1) << "Will set the walltime timer to " << limit;
      absl::Time deadline = absl::Now() + limit;
      deadline_millis_.store(absl::ToUnixMillis(deadline),
                             std::memory_order_relaxed);
      NotifyMonitor();
    }
  }

 private:
  // Waits for events from monitored clients and signals from the main process.
  void RunInternal() override;
  void Join() override;
  void Run();

  bool InitSetupUnotify();
  bool InitSetupNotifyEventFd();
  // Kills the main traced PID with SIGKILL.
  // Returns false if an error occured and process could not be killed.
  bool KillSandboxee();
  void KillInit();

  void HandleUnotify();
  void SetExitStatusFromStatusPipe();

  void MaybeGetStackTrace(pid_t pid, Result::StatusEnum status);
  absl::StatusOr<std::vector<std::string>> GetStackTrace(pid_t pid);

  // Notifies monitor about a state change
  void NotifyMonitor();

  absl::Notification setup_notification_;
  sapi::file_util::fileops::FDCloser seccomp_notify_fd_;
  sapi::file_util::fileops::FDCloser monitor_notify_fd_;
  // Deadline in Unix millis
  std::atomic<int64_t> deadline_millis_{0};
  // False iff external kill is requested
  std::atomic_flag external_kill_request_flag_ = ATOMIC_FLAG_INIT;
  // False iff dump stack is requested
  std::atomic_flag dump_stack_request_flag_ = ATOMIC_FLAG_INIT;

  // Was external kill sent to the sandboxee
  bool external_kill_ = false;
  // Network violation occurred and process of killing sandboxee started
  bool network_violation_ = false;
  // Is the sandboxee timed out
  bool timed_out_ = false;

  // Monitor thread object.
  std::unique_ptr<std::thread> thread_;

  // Synchronizes monitor thread deletion and notifying the monitor.
  absl::Mutex notify_mutex_;

  size_t req_size_;
  std::unique_ptr<seccomp_notif, decltype(std::free)*> req_{nullptr, std::free};
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_MONITOR_UNOTIFY_H_
