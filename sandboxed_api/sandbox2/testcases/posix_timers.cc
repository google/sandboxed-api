#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <string>

#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

ABSL_FLAG(std::string, sigev_notify_kind, "",
          "The C name for the kind of POSIX timer to create (sigev_notify), or "
          "\"syscall(SIGEV_THREAD)\" for a manual syscall approach which "
          "checks that no threads were created.");

int main(int argc, char* argv[]) {
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  static std::atomic<bool> timer_expired(false);
  static std::atomic<pid_t> tid(0);
  static_assert(std::atomic<pid_t>::is_always_lock_free);
  // Handle SIGPROF by recording that it arrived.
  signal(
      SIGPROF, +[](int) {
        timer_expired.store(true);
        tid.store(syscall(__NR_gettid));
      });

  const std::string sigev_notify_kind = absl::GetFlag(FLAGS_sigev_notify_kind);
  struct sigevent sev {};
  sev.sigev_signo = SIGPROF;

  if (sigev_notify_kind == "SIGEV_THREAD" ||
      sigev_notify_kind == "syscall(SIGEV_THREAD)") {
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = +[](sigval_t) {
      timer_expired.store(true);
      tid.store(syscall(__NR_gettid));
    };
  } else if (sigev_notify_kind == "SIGEV_SIGNAL") {
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGPROF;
  } else if (sigev_notify_kind == "SIGEV_NONE") {
    sev.sigev_notify = SIGEV_NONE;
  } else if (sigev_notify_kind == "SIGEV_THREAD_ID") {
    sev.sigev_notify = SIGEV_THREAD_ID;
    sev.sigev_signo = SIGPROF;
#ifndef sigev_notify_thread_id
    sev._sigev_un._tid = syscall(__NR_gettid);
#else
    sev.sigev_notify_thread_id = syscall(__NR_gettid);
#endif
  } else {
    LOG(QFATAL) << "Invalid --sigev_notify_kind: " << sigev_notify_kind;
  }

  struct itimerspec timerspec {};
  timerspec.it_interval.tv_sec = 0;
  timerspec.it_interval.tv_nsec = 1'000'000;
  timerspec.it_value.tv_sec = 0;
  timerspec.it_value.tv_nsec = 1'000'000;

  if (sigev_notify_kind == "syscall(SIGEV_THREAD)") {
    // Use raw syscalls.
    int timer;
    PCHECK(syscall(__NR_timer_create, CLOCK_REALTIME, &sev, &timer) == 0);
    PCHECK(syscall(__NR_timer_settime, timer, 0, &timerspec, nullptr) == 0);

    // Long enough to effectively guarantee that we see the notification.
    absl::SleepFor(absl::Milliseconds(30));

    PCHECK(syscall(__NR_timer_gettime, timer, &timerspec) == 0);
    PCHECK(syscall(__NR_timer_getoverrun, timer) != -1);

    PCHECK(syscall(__NR_timer_delete, timer) == 0);

    // The syscall with SIGEV_THREAD doesn't spawn a thread, which we can verify
    // by checking that the thread ID is the main thread.
    CHECK_EQ(tid.load(), syscall(__NR_gettid));
  } else {
    timer_t timer;
    PCHECK(timer_create(CLOCK_REALTIME, &sev, &timer) == 0);
    PCHECK(timer_settime(timer, 0, &timerspec, nullptr) == 0);

    // Long enough to effectively guarantee that we see the notification.
    absl::SleepFor(absl::Milliseconds(30));

    PCHECK(timer_gettime(timer, &timerspec) == 0);
    PCHECK(timer_getoverrun(timer) != -1);

    PCHECK(timer_delete(timer) == 0);
  }

  if (sigev_notify_kind == "SIGEV_THREAD" ||
      sigev_notify_kind == "syscall(SIGEV_THREAD)" ||
      sigev_notify_kind == "SIGEV_THREAD_ID" ||
      sigev_notify_kind == "SIGEV_SIGNAL") {
    CHECK(timer_expired.load());
  } else {
    CHECK_EQ(sigev_notify_kind, "SIGEV_NONE");
    CHECK(!timer_expired.load());
  }

  return 0;
}
