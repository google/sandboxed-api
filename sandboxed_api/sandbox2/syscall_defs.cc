#include "sandboxed_api/sandbox2/syscall_defs.h"

#include <glog/logging.h>
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "sandboxed_api/sandbox2/util.h"

namespace sandbox2 {

namespace {

std::string GetArgumentDescription(uint64_t value, SyscallTable::ArgType type,
                              pid_t pid) {
  std::string ret = absl::StrFormat("%#x", value);
  switch (type) {
    case SyscallTable::kOct:
      absl::StrAppendFormat(&ret, " [\\0%o]", value);
      break;
    case SyscallTable::kPath: {
      auto path_or = util::ReadCPathFromPid(pid, value);
      if (path_or.ok()) {
        std::string path = path_or.ValueOrDie();
        absl::StrAppendFormat(&ret, " ['%s']", absl::CHexEscape(path));
      } else {
        absl::StrAppend(&ret, " [unreadable path]");
      }
    } break;
    case SyscallTable::kInt:
      absl::StrAppendFormat(&ret, " [%d]", value);
      break;
    default:
      break;
  }
  return ret;
}

}  // namespace

std::vector<std::string> SyscallTable::Entry::GetArgumentsDescription(
    const uint64_t values[], pid_t pid) const {
  int num_args = GetNumArgs();
  std::vector<std::string> rv;
  rv.reserve(num_args);
  for (int i = 0; i < num_args; ++i) {
    rv.emplace_back(GetArgumentDescription(values[i], arg_types[i], pid));
  }
  return rv;
}

// TODO(wiktorg): Rewrite it without macros - might be easier after C++17 switch
#define SYSCALL_HELPER(n, name, arg1, arg2, arg3, arg4, arg5, arg6, ...) \
  {                                                                      \
    name, n, {                                                           \
      SyscallTable::arg1, SyscallTable::arg2, SyscallTable::arg3,        \
          SyscallTable::arg4, SyscallTable::arg5, SyscallTable::arg6     \
    }                                                                    \
  }
#define SYSCALL_NUM_ARGS_HELPER(_0, _1, _2, _3, _4, _5, _6, N, ...) N
#define SYSCALL_NUM_ARGS(...) \
  SYSCALL_NUM_ARGS_HELPER(__VA_ARGS__, 6, 5, 4, 3, 2, 1, 0)
#define SYSCALL(...)                                                           \
  SYSCALL_HELPER(SYSCALL_NUM_ARGS(__VA_ARGS__), __VA_ARGS__, kGen, kGen, kGen, \
                 kGen, kGen, kGen)
#define SYSCALL_WITH_UNKNOWN_ARGS(name) \
  SYSCALL_HELPER(-1, name, kGen, kGen, kGen, kGen, kGen, kGen)

#define SYSCALLS_UNUSED(name) SYSCALL(name, kHex, kHex, kHex, kHex, kHex, kHex)

#define SYSCALLS_UNUSED0_9(prefix)                              \
  SYSCALLS_UNUSED(prefix "0"), SYSCALLS_UNUSED(prefix "1"),     \
      SYSCALLS_UNUSED(prefix "2"), SYSCALLS_UNUSED(prefix "3"), \
      SYSCALLS_UNUSED(prefix "4"), SYSCALLS_UNUSED(prefix "5"), \
      SYSCALLS_UNUSED(prefix "6"), SYSCALLS_UNUSED(prefix "7"), \
      SYSCALLS_UNUSED(prefix "8"), SYSCALLS_UNUSED(prefix "9")

#define SYSCALLS_UNUSED00_49(prefix)                                  \
  SYSCALLS_UNUSED0_9(prefix "0"), SYSCALLS_UNUSED0_9(prefix "1"),     \
      SYSCALLS_UNUSED0_9(prefix "2"), SYSCALLS_UNUSED0_9(prefix "3"), \
      SYSCALLS_UNUSED0_9(prefix "4")
#define SYSCALLS_UNUSED50_99(prefix)                                  \
  SYSCALLS_UNUSED0_9(prefix "5"), SYSCALLS_UNUSED0_9(prefix "6"),     \
      SYSCALLS_UNUSED0_9(prefix "7"), SYSCALLS_UNUSED0_9(prefix "8"), \
      SYSCALLS_UNUSED0_9(prefix "9")
#define SYSCALLS_UNUSED00_99(prefix) \
  SYSCALLS_UNUSED00_49(prefix), SYSCALLS_UNUSED50_99(prefix)

#if defined(__x86_64__)
// Syscall description table for Linux x86_64
const absl::Span<const SyscallTable::Entry> SyscallTable::kSyscallDataX8664 = {
    SYSCALL("read", kInt, kHex, kInt),                                 // 0
    SYSCALL("write", kInt, kHex, kInt),                                // 1
    SYSCALL("open", kPath, kHex, kOct),                                // 2
    SYSCALL("close", kInt),                                            // 3
    SYSCALL("stat", kPath, kGen),                                      // 4
    SYSCALL("fstat", kInt, kHex),                                      // 5
    SYSCALL("lstat", kPath, kGen),                                     // 6
    SYSCALL_WITH_UNKNOWN_ARGS("poll"),                                 // 7
    SYSCALL_WITH_UNKNOWN_ARGS("lseek"),                                // 8
    SYSCALL("mmap", kHex, kInt, kHex, kHex, kInt, kInt),               // 9
    SYSCALL("mprotect", kHex, kHex, kHex),                             // 10
    SYSCALL("munmap", kHex, kHex),                                     // 11
    SYSCALL("brk", kHex),                                              // 12
    SYSCALL("rt_sigaction", kSignal, kHex, kHex, kInt),                // 13
    SYSCALL_WITH_UNKNOWN_ARGS("rt_sigprocmask"),                       // 14
    SYSCALL("rt_sigreturn"),                                           // 15
    SYSCALL_WITH_UNKNOWN_ARGS("ioctl"),                                // 16
    SYSCALL_WITH_UNKNOWN_ARGS("pread64"),                              // 17
    SYSCALL_WITH_UNKNOWN_ARGS("pwrite64"),                             // 18
    SYSCALL_WITH_UNKNOWN_ARGS("readv"),                                // 19
    SYSCALL_WITH_UNKNOWN_ARGS("writev"),                               // 20
    SYSCALL("access", kPath, kHex),                                    // 21
    SYSCALL_WITH_UNKNOWN_ARGS("pipe"),                                 // 22
    SYSCALL_WITH_UNKNOWN_ARGS("select"),                               // 23
    SYSCALL_WITH_UNKNOWN_ARGS("sched_yield"),                          // 24
    SYSCALL_WITH_UNKNOWN_ARGS("mremap"),                               // 25
    SYSCALL_WITH_UNKNOWN_ARGS("msync"),                                // 26
    SYSCALL_WITH_UNKNOWN_ARGS("mincore"),                              // 27
    SYSCALL_WITH_UNKNOWN_ARGS("madvise"),                              // 28
    SYSCALL_WITH_UNKNOWN_ARGS("shmget"),                               // 29
    SYSCALL_WITH_UNKNOWN_ARGS("shmat"),                                // 30
    SYSCALL_WITH_UNKNOWN_ARGS("shmctl"),                               // 31
    SYSCALL_WITH_UNKNOWN_ARGS("dup"),                                  // 32
    SYSCALL("dup2", kGen, kGen),                                       // 33
    SYSCALL("pause"),                                                  // 34
    SYSCALL("nanosleep", kHex, kHex),                                  // 35
    SYSCALL_WITH_UNKNOWN_ARGS("getitimer"),                            // 36
    SYSCALL("alarm", kInt),                                            // 37
    SYSCALL_WITH_UNKNOWN_ARGS("setitimer"),                            // 38
    SYSCALL("getpid"),                                                 // 39
    SYSCALL_WITH_UNKNOWN_ARGS("sendfile"),                             // 40
    SYSCALL("socket", kAddressFamily, kInt, kInt),                     // 41
    SYSCALL("connect", kInt, kSockaddr, kInt),                         // 42
    SYSCALL_WITH_UNKNOWN_ARGS("accept"),                               // 43
    SYSCALL("sendto", kInt, kGen, kInt, kHex, kSockaddr, kInt),        // 44
    SYSCALL_WITH_UNKNOWN_ARGS("recvfrom"),                             // 45
    SYSCALL("sendmsg", kInt, kSockmsghdr, kHex),                       // 46
    SYSCALL_WITH_UNKNOWN_ARGS("recvmsg"),                              // 47
    SYSCALL_WITH_UNKNOWN_ARGS("shutdown"),                             // 48
    SYSCALL_WITH_UNKNOWN_ARGS("bind"),                                 // 49
    SYSCALL_WITH_UNKNOWN_ARGS("listen"),                               // 50
    SYSCALL_WITH_UNKNOWN_ARGS("getsockname"),                          // 51
    SYSCALL_WITH_UNKNOWN_ARGS("getpeername"),                          // 52
    SYSCALL_WITH_UNKNOWN_ARGS("socketpair"),                           // 53
    SYSCALL_WITH_UNKNOWN_ARGS("setsockopt"),                           // 54
    SYSCALL_WITH_UNKNOWN_ARGS("getsockopt"),                           // 55
    SYSCALL("clone", kCloneFlag, kHex, kHex, kHex, kHex),              // 56
    SYSCALL("fork"),                                                   // 57
    SYSCALL("vfork"),                                                  // 58
    SYSCALL("execve", kPath, kHex, kHex),                              // 59
    SYSCALL("exit", kInt),                                             // 60
    SYSCALL("wait4", kInt, kHex, kHex, kHex),                          // 61
    SYSCALL("kill", kInt, kSignal),                                    // 62
    SYSCALL_WITH_UNKNOWN_ARGS("uname"),                                // 63
    SYSCALL_WITH_UNKNOWN_ARGS("semget"),                               // 64
    SYSCALL_WITH_UNKNOWN_ARGS("semop"),                                // 65
    SYSCALL_WITH_UNKNOWN_ARGS("semctl"),                               // 66
    SYSCALL_WITH_UNKNOWN_ARGS("shmdt"),                                // 67
    SYSCALL_WITH_UNKNOWN_ARGS("msgget"),                               // 68
    SYSCALL_WITH_UNKNOWN_ARGS("msgsnd"),                               // 69
    SYSCALL_WITH_UNKNOWN_ARGS("msgrcv"),                               // 70
    SYSCALL_WITH_UNKNOWN_ARGS("msgctl"),                               // 71
    SYSCALL_WITH_UNKNOWN_ARGS("fcntl"),                                // 72
    SYSCALL_WITH_UNKNOWN_ARGS("flock"),                                // 73
    SYSCALL_WITH_UNKNOWN_ARGS("fsync"),                                // 74
    SYSCALL_WITH_UNKNOWN_ARGS("fdatasync"),                            // 75
    SYSCALL("truncate", kPath, kInt),                                  // 76
    SYSCALL_WITH_UNKNOWN_ARGS("ftruncate"),                            // 77
    SYSCALL_WITH_UNKNOWN_ARGS("getdents"),                             // 78
    SYSCALL_WITH_UNKNOWN_ARGS("getcwd"),                               // 79
    SYSCALL("chdir", kPath),                                           // 80
    SYSCALL_WITH_UNKNOWN_ARGS("fchdir"),                               // 81
    SYSCALL("rename", kPath, kPath),                                   // 82
    SYSCALL("mkdir", kPath, kOct),                                     // 83
    SYSCALL("rmdir", kPath),                                           // 84
    SYSCALL("creat", kPath, kOct),                                     // 85
    SYSCALL("link", kPath, kPath),                                     // 86
    SYSCALL("unlink", kPath),                                          // 87
    SYSCALL("symlink", kPath, kPath),                                  // 88
    SYSCALL("readlink", kPath, kGen, kInt),                            // 89
    SYSCALL("chmod", kPath, kOct),                                     // 90
    SYSCALL_WITH_UNKNOWN_ARGS("fchmod"),                               // 91
    SYSCALL("chown", kPath, kInt, kInt),                               // 92
    SYSCALL_WITH_UNKNOWN_ARGS("fchown"),                               // 93
    SYSCALL("lchown", kPath, kInt, kInt),                              // 94
    SYSCALL("umask", kHex),                                            // 95
    SYSCALL("gettimeofday", kHex, kHex),                               // 96
    SYSCALL_WITH_UNKNOWN_ARGS("getrlimit"),                            // 97
    SYSCALL_WITH_UNKNOWN_ARGS("getrusage"),                            // 98
    SYSCALL_WITH_UNKNOWN_ARGS("sysinfo"),                              // 99
    SYSCALL_WITH_UNKNOWN_ARGS("times"),                                // 100
    SYSCALL("ptrace", kGen, kGen, kGen),                               // 101
    SYSCALL_WITH_UNKNOWN_ARGS("getuid"),                               // 102
    SYSCALL_WITH_UNKNOWN_ARGS("syslog"),                               // 103
    SYSCALL_WITH_UNKNOWN_ARGS("getgid"),                               // 104
    SYSCALL_WITH_UNKNOWN_ARGS("setuid"),                               // 105
    SYSCALL_WITH_UNKNOWN_ARGS("setgid"),                               // 106
    SYSCALL_WITH_UNKNOWN_ARGS("geteuid"),                              // 107
    SYSCALL_WITH_UNKNOWN_ARGS("getegid"),                              // 108
    SYSCALL_WITH_UNKNOWN_ARGS("setpgid"),                              // 109
    SYSCALL_WITH_UNKNOWN_ARGS("getppid"),                              // 110
    SYSCALL_WITH_UNKNOWN_ARGS("getpgrp"),                              // 111
    SYSCALL_WITH_UNKNOWN_ARGS("setsid"),                               // 112
    SYSCALL_WITH_UNKNOWN_ARGS("setreuid"),                             // 113
    SYSCALL_WITH_UNKNOWN_ARGS("setregid"),                             // 114
    SYSCALL_WITH_UNKNOWN_ARGS("getgroups"),                            // 115
    SYSCALL_WITH_UNKNOWN_ARGS("setgroups"),                            // 116
    SYSCALL_WITH_UNKNOWN_ARGS("setresuid"),                            // 117
    SYSCALL_WITH_UNKNOWN_ARGS("getresuid"),                            // 118
    SYSCALL_WITH_UNKNOWN_ARGS("setresgid"),                            // 119
    SYSCALL_WITH_UNKNOWN_ARGS("getresgid"),                            // 120
    SYSCALL_WITH_UNKNOWN_ARGS("getpgid"),                              // 121
    SYSCALL_WITH_UNKNOWN_ARGS("setfsuid"),                             // 122
    SYSCALL_WITH_UNKNOWN_ARGS("setfsgid"),                             // 123
    SYSCALL_WITH_UNKNOWN_ARGS("getsid"),                               // 124
    SYSCALL_WITH_UNKNOWN_ARGS("capget"),                               // 125
    SYSCALL_WITH_UNKNOWN_ARGS("capset"),                               // 126
    SYSCALL_WITH_UNKNOWN_ARGS("rt_sigpending"),                        // 127
    SYSCALL_WITH_UNKNOWN_ARGS("rt_sigtimedwait"),                      // 128
    SYSCALL_WITH_UNKNOWN_ARGS("rt_sigqueueinfo"),                      // 129
    SYSCALL_WITH_UNKNOWN_ARGS("rt_sigsuspend"),                        // 130
    SYSCALL_WITH_UNKNOWN_ARGS("sigaltstack"),                          // 131
    SYSCALL_WITH_UNKNOWN_ARGS("utime"),                                // 132
    SYSCALL("mknod", kPath, kOct, kHex),                               // 133
    SYSCALL("uselib", kPath),                                          // 134
    SYSCALL_WITH_UNKNOWN_ARGS("personality"),                          // 135
    SYSCALL_WITH_UNKNOWN_ARGS("ustat"),                                // 136
    SYSCALL_WITH_UNKNOWN_ARGS("statfs"),                               // 137
    SYSCALL_WITH_UNKNOWN_ARGS("fstatfs"),                              // 138
    SYSCALL_WITH_UNKNOWN_ARGS("sysfs"),                                // 139
    SYSCALL_WITH_UNKNOWN_ARGS("getpriority"),                          // 140
    SYSCALL_WITH_UNKNOWN_ARGS("setpriority"),                          // 141
    SYSCALL_WITH_UNKNOWN_ARGS("sched_setparam"),                       // 142
    SYSCALL_WITH_UNKNOWN_ARGS("sched_getparam"),                       // 143
    SYSCALL_WITH_UNKNOWN_ARGS("sched_setscheduler"),                   // 144
    SYSCALL_WITH_UNKNOWN_ARGS("sched_getscheduler"),                   // 145
    SYSCALL_WITH_UNKNOWN_ARGS("sched_get_priority_max"),               // 146
    SYSCALL_WITH_UNKNOWN_ARGS("sched_get_priority_min"),               // 147
    SYSCALL_WITH_UNKNOWN_ARGS("sched_rr_get_interval"),                // 148
    SYSCALL_WITH_UNKNOWN_ARGS("mlock"),                                // 149
    SYSCALL_WITH_UNKNOWN_ARGS("munlock"),                              // 150
    SYSCALL_WITH_UNKNOWN_ARGS("mlockall"),                             // 151
    SYSCALL_WITH_UNKNOWN_ARGS("munlockall"),                           // 152
    SYSCALL_WITH_UNKNOWN_ARGS("vhangup"),                              // 153
    SYSCALL_WITH_UNKNOWN_ARGS("modify_ldt"),                           // 154
    SYSCALL("pivot_root", kPath, kPath),                               // 155
    SYSCALL_WITH_UNKNOWN_ARGS("_sysctl"),                              // 156
    SYSCALL("prctl", kInt, kHex, kHex, kHex, kHex),                    // 157
    SYSCALL("arch_prctl", kInt, kHex),                                 // 158
    SYSCALL_WITH_UNKNOWN_ARGS("adjtimex"),                             // 159
    SYSCALL_WITH_UNKNOWN_ARGS("setrlimit"),                            // 160
    SYSCALL("chroot", kPath),                                          // 161
    SYSCALL_WITH_UNKNOWN_ARGS("sync"),                                 // 162
    SYSCALL("acct", kPath),                                            // 163
    SYSCALL("settimeofday", kHex, kHex),                               // 164
    SYSCALL("mount", kPath, kPath, kString, kHex, kGen),               // 165
    SYSCALL("umount2", kPath, kHex),                                   // 166
    SYSCALL("swapon", kPath, kHex),                                    // 167
    SYSCALL("swapoff", kPath),                                         // 168
    SYSCALL_WITH_UNKNOWN_ARGS("reboot"),                               // 169
    SYSCALL_WITH_UNKNOWN_ARGS("sethostname"),                          // 170
    SYSCALL_WITH_UNKNOWN_ARGS("setdomainname"),                        // 171
    SYSCALL_WITH_UNKNOWN_ARGS("iopl"),                                 // 172
    SYSCALL_WITH_UNKNOWN_ARGS("ioperm"),                               // 173
    SYSCALL_WITH_UNKNOWN_ARGS("create_module"),                        // 174
    SYSCALL_WITH_UNKNOWN_ARGS("init_module"),                          // 175
    SYSCALL_WITH_UNKNOWN_ARGS("delete_module"),                        // 176
    SYSCALL_WITH_UNKNOWN_ARGS("get_kernel_syms"),                      // 177
    SYSCALL_WITH_UNKNOWN_ARGS("query_module"),                         // 178
    SYSCALL("quotactl", kInt, kPath, kInt),                            // 179
    SYSCALL_WITH_UNKNOWN_ARGS("nfsservctl"),                           // 180
    SYSCALL_WITH_UNKNOWN_ARGS("getpmsg"),                              // 181
    SYSCALL_WITH_UNKNOWN_ARGS("putpmsg"),                              // 182
    SYSCALL_WITH_UNKNOWN_ARGS("afs_syscall"),                          // 183
    SYSCALL_WITH_UNKNOWN_ARGS("tuxcall"),                              // 184
    SYSCALL_WITH_UNKNOWN_ARGS("security"),                             // 185
    SYSCALL("gettid"),                                                 // 186
    SYSCALL_WITH_UNKNOWN_ARGS("readahead"),                            // 187
    SYSCALL("setxattr", kPath, kString, kGen, kInt, kHex),             // 188
    SYSCALL("lsetxattr", kPath, kString, kGen, kInt, kHex),            // 189
    SYSCALL_WITH_UNKNOWN_ARGS("fsetxattr"),                            // 190
    SYSCALL("getxattr", kPath, kString, kGen, kInt),                   // 191
    SYSCALL("lgetxattr", kPath, kString, kGen, kInt),                  // 192
    SYSCALL_WITH_UNKNOWN_ARGS("fgetxattr"),                            // 193
    SYSCALL("listxattr", kPath, kGen, kInt),                           // 194
    SYSCALL("llistxattr", kPath, kGen, kInt),                          // 195
    SYSCALL_WITH_UNKNOWN_ARGS("flistxattr"),                           // 196
    SYSCALL("removexattr", kPath, kString),                            // 197
    SYSCALL_WITH_UNKNOWN_ARGS("lremovexattr"),                         // 198
    SYSCALL_WITH_UNKNOWN_ARGS("fremovexattr"),                         // 199
    SYSCALL("tkill", kInt, kSignal),                                   // 200
    SYSCALL("time", kHex),                                             // 201
    SYSCALL_WITH_UNKNOWN_ARGS("futex"),                                // 202
    SYSCALL_WITH_UNKNOWN_ARGS("sched_setaffinity"),                    // 203
    SYSCALL_WITH_UNKNOWN_ARGS("sched_getaffinity"),                    // 204
    SYSCALL("set_thread_area", kHex),                                  // 205
    SYSCALL_WITH_UNKNOWN_ARGS("io_setup"),                             // 206
    SYSCALL_WITH_UNKNOWN_ARGS("io_destroy"),                           // 207
    SYSCALL_WITH_UNKNOWN_ARGS("io_getevents"),                         // 208
    SYSCALL_WITH_UNKNOWN_ARGS("io_submit"),                            // 209
    SYSCALL_WITH_UNKNOWN_ARGS("io_cancel"),                            // 210
    SYSCALL("get_thread_area", kHex),                                  // 211
    SYSCALL_WITH_UNKNOWN_ARGS("lookup_dcookie"),                       // 212
    SYSCALL_WITH_UNKNOWN_ARGS("epoll_create"),                         // 213
    SYSCALL_WITH_UNKNOWN_ARGS("epoll_ctl_old"),                        // 214
    SYSCALL_WITH_UNKNOWN_ARGS("epoll_wait_old"),                       // 215
    SYSCALL_WITH_UNKNOWN_ARGS("remap_file_pages"),                     // 216
    SYSCALL_WITH_UNKNOWN_ARGS("getdents64"),                           // 217
    SYSCALL("set_tid_address", kHex),                                  // 218
    SYSCALL_WITH_UNKNOWN_ARGS("restart_syscall"),                      // 219
    SYSCALL_WITH_UNKNOWN_ARGS("semtimedop"),                           // 220
    SYSCALL_WITH_UNKNOWN_ARGS("fadvise64"),                            // 221
    SYSCALL_WITH_UNKNOWN_ARGS("timer_create"),                         // 222
    SYSCALL_WITH_UNKNOWN_ARGS("timer_settime"),                        // 223
    SYSCALL_WITH_UNKNOWN_ARGS("timer_gettime"),                        // 224
    SYSCALL_WITH_UNKNOWN_ARGS("timer_getoverrun"),                     // 225
    SYSCALL_WITH_UNKNOWN_ARGS("timer_delete"),                         // 226
    SYSCALL_WITH_UNKNOWN_ARGS("clock_settime"),                        // 227
    SYSCALL_WITH_UNKNOWN_ARGS("clock_gettime"),                        // 228
    SYSCALL_WITH_UNKNOWN_ARGS("clock_getres"),                         // 229
    SYSCALL_WITH_UNKNOWN_ARGS("clock_nanosleep"),                      // 230
    SYSCALL("exit_group", kInt),                                       // 231
    SYSCALL_WITH_UNKNOWN_ARGS("epoll_wait"),                           // 232
    SYSCALL_WITH_UNKNOWN_ARGS("epoll_ctl"),                            // 233
    SYSCALL("tgkill", kInt, kInt, kSignal),                            // 234
    SYSCALL_WITH_UNKNOWN_ARGS("utimes"),                               // 235
    SYSCALL_WITH_UNKNOWN_ARGS("vserver"),                              // 236
    SYSCALL_WITH_UNKNOWN_ARGS("mbind"),                                // 237
    SYSCALL_WITH_UNKNOWN_ARGS("set_mempolicy"),                        // 238
    SYSCALL_WITH_UNKNOWN_ARGS("get_mempolicy"),                        // 239
    SYSCALL_WITH_UNKNOWN_ARGS("mq_open"),                              // 240
    SYSCALL_WITH_UNKNOWN_ARGS("mq_unlink"),                            // 241
    SYSCALL_WITH_UNKNOWN_ARGS("mq_timedsend"),                         // 242
    SYSCALL_WITH_UNKNOWN_ARGS("mq_timedreceive"),                      // 243
    SYSCALL_WITH_UNKNOWN_ARGS("mq_notify"),                            // 244
    SYSCALL_WITH_UNKNOWN_ARGS("mq_getsetattr"),                        // 245
    SYSCALL_WITH_UNKNOWN_ARGS("kexec_load"),                           // 246
    SYSCALL_WITH_UNKNOWN_ARGS("waitid"),                               // 247
    SYSCALL_WITH_UNKNOWN_ARGS("add_key"),                              // 248
    SYSCALL_WITH_UNKNOWN_ARGS("request_key"),                          // 249
    SYSCALL_WITH_UNKNOWN_ARGS("keyctl"),                               // 250
    SYSCALL_WITH_UNKNOWN_ARGS("ioprio_set"),                           // 251
    SYSCALL_WITH_UNKNOWN_ARGS("ioprio_get"),                           // 252
    SYSCALL_WITH_UNKNOWN_ARGS("inotify_init"),                         // 253
    SYSCALL_WITH_UNKNOWN_ARGS("inotify_add_watch"),                    // 254
    SYSCALL_WITH_UNKNOWN_ARGS("inotify_rm_watch"),                     // 255
    SYSCALL_WITH_UNKNOWN_ARGS("migrate_pages"),                        // 256
    SYSCALL("openat", kGen, kPath, kOct, kHex),                        // 257
    SYSCALL("mkdirat", kGen, kPath),                                   // 258
    SYSCALL("mknodat", kGen, kPath),                                   // 259
    SYSCALL("fchownat", kGen, kPath),                                  // 260
    SYSCALL("futimesat", kGen, kPath),                                 // 261
    SYSCALL("newfstatat", kGen, kPath),                                // 262
    SYSCALL("unlinkat", kGen, kPath),                                  // 263
    SYSCALL("renameat", kGen, kPath, kGen, kPath),                     // 264
    SYSCALL("linkat", kGen, kPath, kGen, kPath),                       // 265
    SYSCALL("symlinkat", kPath, kGen, kPath),                          // 266
    SYSCALL("readlinkat", kGen, kPath),                                // 267
    SYSCALL("fchmodat", kGen, kPath),                                  // 268
    SYSCALL("faccessat", kGen, kPath),                                 // 269
    SYSCALL_WITH_UNKNOWN_ARGS("pselect6"),                             // 270
    SYSCALL_WITH_UNKNOWN_ARGS("ppoll"),                                // 271
    SYSCALL_WITH_UNKNOWN_ARGS("unshare"),                              // 272
    SYSCALL("set_robust_list", kGen, kGen),                            // 273
    SYSCALL_WITH_UNKNOWN_ARGS("get_robust_list"),                      // 274
    SYSCALL_WITH_UNKNOWN_ARGS("splice"),                               // 275
    SYSCALL_WITH_UNKNOWN_ARGS("tee"),                                  // 276
    SYSCALL_WITH_UNKNOWN_ARGS("sync_file_range"),                      // 277
    SYSCALL_WITH_UNKNOWN_ARGS("vmsplice"),                             // 278
    SYSCALL_WITH_UNKNOWN_ARGS("move_pages"),                           // 279
    SYSCALL_WITH_UNKNOWN_ARGS("utimensat"),                            // 280
    SYSCALL_WITH_UNKNOWN_ARGS("epoll_pwait"),                          // 281
    SYSCALL_WITH_UNKNOWN_ARGS("signalfd"),                             // 282
    SYSCALL_WITH_UNKNOWN_ARGS("timerfd_create"),                       // 283
    SYSCALL_WITH_UNKNOWN_ARGS("eventfd"),                              // 284
    SYSCALL_WITH_UNKNOWN_ARGS("fallocate"),                            // 285
    SYSCALL_WITH_UNKNOWN_ARGS("timerfd_settime"),                      // 286
    SYSCALL_WITH_UNKNOWN_ARGS("timerfd_gettime"),                      // 287
    SYSCALL_WITH_UNKNOWN_ARGS("accept4"),                              // 288
    SYSCALL_WITH_UNKNOWN_ARGS("signalfd4"),                            // 289
    SYSCALL_WITH_UNKNOWN_ARGS("eventfd2"),                             // 290
    SYSCALL_WITH_UNKNOWN_ARGS("epoll_create1"),                        // 291
    SYSCALL("dup3", kGen, kGen, kGen),                                 // 292
    SYSCALL_WITH_UNKNOWN_ARGS("pipe2"),                                // 293
    SYSCALL_WITH_UNKNOWN_ARGS("inotify_init1"),                        // 294
    SYSCALL_WITH_UNKNOWN_ARGS("preadv"),                               // 295
    SYSCALL_WITH_UNKNOWN_ARGS("pwritev"),                              // 296
    SYSCALL_WITH_UNKNOWN_ARGS("rt_tgsigqueueinfo"),                    // 297
    SYSCALL_WITH_UNKNOWN_ARGS("perf_event_open"),                      // 298
    SYSCALL("recvmmsg", kInt, kHex, kHex, kHex),                       // 299
    SYSCALL("fanotify_init", kHex, kHex, kInt),                        // 300
    SYSCALL("fanotify_mark", kInt, kHex, kInt, kPath),                 // 301
    SYSCALL("prlimit64", kInt, kInt, kHex, kHex),                      // 302
    SYSCALL("name_to_handle_at", kInt, kGen, kHex, kHex, kHex),        // 303
    SYSCALL("open_by_handle_at", kInt, kHex, kHex),                    // 304
    SYSCALL("clock_adjtime", kInt, kHex),                              // 305
    SYSCALL("syncfs", kInt),                                           // 306
    SYSCALL("sendmmsg", kInt, kHex, kInt, kHex),                       // 307
    SYSCALL("setns", kInt, kHex),                                      // 308
    SYSCALL("getcpu", kHex, kHex, kHex),                               // 309
    SYSCALL("process_vm_readv", kInt, kHex, kInt, kHex, kInt, kInt),   // 310
    SYSCALL("process_vm_writev", kInt, kHex, kInt, kHex, kInt, kInt),  // 311
    SYSCALL("kcmp", kInt, kInt, kInt, kHex, kHex),                     // 312
    SYSCALL("finit_module", kInt, kPath, kHex),                        // 313
    SYSCALL("sched_setattr", kGen, kGen, kGen, kGen, kGen, kGen),      // 314
    SYSCALL("sched_getattr", kGen, kGen, kGen, kGen, kGen, kGen),      // 315
    SYSCALL("renameat2", kGen, kPath, kGen, kPath, kGen, kGen),        // 316
    SYSCALL("seccomp", kGen, kGen, kGen, kGen, kGen, kGen),            // 317
    SYSCALL("getrandom", kGen, kGen, kGen, kGen, kGen, kGen),          // 318
    SYSCALL("memfd_create", kGen, kGen, kGen, kGen, kGen, kGen),       // 319
    SYSCALL("kexec_file_load", kGen, kGen, kGen, kGen, kGen, kGen),    // 320
    SYSCALL("bpf", kHex, kHex, kHex, kHex, kHex, kHex),                // 321
    SYSCALL("execveat", kHex, kPath, kHex, kHex, kHex),                // 322
    SYSCALL("userfaultfd", kHex),                                      // 323
    SYSCALL("membarrier", kHex, kHex),                                 // 324
};

const absl::Span<const SyscallTable::Entry> SyscallTable::kSyscallDataX8632 = {
    SYSCALL("restart_syscall", kHex, kHex, kHex, kHex, kHex, kHex),     // 0
    SYSCALL("exit", kHex, kHex, kHex, kHex, kHex, kHex),                // 1
    SYSCALL("fork", kHex, kHex, kHex, kHex, kHex, kHex),                // 2
    SYSCALL("read", kHex, kHex, kHex, kHex, kHex, kHex),                // 3
    SYSCALL("write", kHex, kHex, kHex, kHex, kHex, kHex),               // 4
    SYSCALL("open", kPath, kHex, kOct, kHex, kHex, kHex),               // 5
    SYSCALL("close", kHex, kHex, kHex, kHex, kHex, kHex),               // 6
    SYSCALL("waitpid", kHex, kHex, kHex, kHex, kHex, kHex),             // 7
    SYSCALL("creat", kPath, kHex, kHex, kHex, kHex, kHex),              // 8
    SYSCALL("link", kPath, kPath, kHex, kHex, kHex, kHex),              // 9
    SYSCALL("unlink", kPath, kHex, kHex, kHex, kHex, kHex),             // 10
    SYSCALL("execve", kPath, kHex, kHex, kHex, kHex, kHex),             // 11
    SYSCALL("chdir", kPath, kHex, kHex, kHex, kHex, kHex),              // 12
    SYSCALL("time", kHex, kHex, kHex, kHex, kHex, kHex),                // 13
    SYSCALL("mknod", kPath, kOct, kHex, kHex, kHex, kHex),              // 14
    SYSCALL("chmod", kPath, kOct, kHex, kHex, kHex, kHex),              // 15
    SYSCALL("lchown", kPath, kInt, kInt, kHex, kHex, kHex),             // 16
    SYSCALL("break", kHex, kHex, kHex, kHex, kHex, kHex),               // 17
    SYSCALL("oldstat", kHex, kHex, kHex, kHex, kHex, kHex),             // 18
    SYSCALL("lseek", kHex, kHex, kHex, kHex, kHex, kHex),               // 19
    SYSCALL("getpid", kHex, kHex, kHex, kHex, kHex, kHex),              // 20
    SYSCALL("mount", kHex, kHex, kHex, kHex, kHex, kHex),               // 21
    SYSCALL("umount", kHex, kHex, kHex, kHex, kHex, kHex),              // 22
    SYSCALL("setuid", kHex, kHex, kHex, kHex, kHex, kHex),              // 23
    SYSCALL("getuid", kHex, kHex, kHex, kHex, kHex, kHex),              // 24
    SYSCALL("stime", kHex, kHex, kHex, kHex, kHex, kHex),               // 25
    SYSCALL("ptrace", kHex, kHex, kHex, kHex),                          // 26
    SYSCALL("alarm", kHex, kHex, kHex, kHex, kHex, kHex),               // 27
    SYSCALL("oldfstat", kHex, kHex, kHex, kHex, kHex, kHex),            // 28
    SYSCALL("pause", kHex, kHex, kHex, kHex, kHex, kHex),               // 29
    SYSCALL("utime", kHex, kHex, kHex, kHex, kHex, kHex),               // 30
    SYSCALL("stty", kHex, kHex, kHex, kHex, kHex, kHex),                // 31
    SYSCALL("gtty", kHex, kHex, kHex, kHex, kHex, kHex),                // 32
    SYSCALL("access", kPath, kHex, kHex, kHex, kHex, kHex),             // 33
    SYSCALL("nice", kHex, kHex, kHex, kHex, kHex, kHex),                // 34
    SYSCALL("ftime", kHex, kHex, kHex, kHex, kHex, kHex),               // 35
    SYSCALL("sync", kHex, kHex, kHex, kHex, kHex, kHex),                // 36
    SYSCALL("kill", kHex, kHex, kHex, kHex, kHex, kHex),                // 37
    SYSCALL("rename", kPath, kPath, kHex, kHex, kHex, kHex),            // 38
    SYSCALL("mkdir", kPath, kHex, kHex, kHex, kHex, kHex),              // 39
    SYSCALL("rmdir", kHex, kHex, kHex, kHex, kHex, kHex),               // 40
    SYSCALL("dup", kHex, kHex, kHex, kHex, kHex, kHex),                 // 41
    SYSCALL("pipe", kHex, kHex, kHex, kHex, kHex, kHex),                // 42
    SYSCALL("times", kHex, kHex, kHex, kHex, kHex, kHex),               // 43
    SYSCALL("prof", kHex, kHex, kHex, kHex, kHex, kHex),                // 44
    SYSCALL("brk", kHex, kHex, kHex, kHex, kHex, kHex),                 // 45
    SYSCALL("setgid", kHex, kHex, kHex, kHex, kHex, kHex),              // 46
    SYSCALL("getgid", kHex, kHex, kHex, kHex, kHex, kHex),              // 47
    SYSCALL("signal", kHex, kHex, kHex, kHex, kHex, kHex),              // 48
    SYSCALL("geteuid", kHex, kHex, kHex, kHex, kHex, kHex),             // 49
    SYSCALL("getegid", kHex, kHex, kHex, kHex, kHex, kHex),             // 50
    SYSCALL("acct", kHex, kHex, kHex, kHex, kHex, kHex),                // 51
    SYSCALL("umount2", kHex, kHex, kHex, kHex, kHex, kHex),             // 52
    SYSCALL("lock", kHex, kHex, kHex, kHex, kHex, kHex),                // 53
    SYSCALL("ioctl", kHex, kHex, kHex, kHex, kHex, kHex),               // 54
    SYSCALL("fcntl", kHex, kHex, kHex, kHex, kHex, kHex),               // 55
    SYSCALL("mpx", kHex, kHex, kHex, kHex, kHex, kHex),                 // 56
    SYSCALL("setpgid", kHex, kHex, kHex, kHex, kHex, kHex),             // 57
    SYSCALL("ulimit", kHex, kHex, kHex, kHex, kHex, kHex),              // 58
    SYSCALL("oldolduname", kHex, kHex, kHex, kHex, kHex, kHex),         // 59
    SYSCALL("umask", kHex, kHex, kHex, kHex, kHex, kHex),               // 60
    SYSCALL("chroot", kHex, kHex, kHex, kHex, kHex, kHex),              // 61
    SYSCALL("ustat", kHex, kHex, kHex, kHex, kHex, kHex),               // 62
    SYSCALL("dup2", kHex, kHex, kHex, kHex, kHex, kHex),                // 63
    SYSCALL("getppid", kHex, kHex, kHex, kHex, kHex, kHex),             // 64
    SYSCALL("getpgrp", kHex, kHex, kHex, kHex, kHex, kHex),             // 65
    SYSCALL("setsid", kHex, kHex, kHex, kHex, kHex, kHex),              // 66
    SYSCALL("sigaction", kHex, kHex, kHex, kHex, kHex, kHex),           // 67
    SYSCALL("sgetmask", kHex, kHex, kHex, kHex, kHex, kHex),            // 68
    SYSCALL("ssetmask", kHex, kHex, kHex, kHex, kHex, kHex),            // 69
    SYSCALL("setreuid", kHex, kHex, kHex, kHex, kHex, kHex),            // 70
    SYSCALL("setregid", kHex, kHex, kHex, kHex, kHex, kHex),            // 71
    SYSCALL("sigsuspend", kHex, kHex, kHex, kHex, kHex, kHex),          // 72
    SYSCALL("sigpending", kHex, kHex, kHex, kHex, kHex, kHex),          // 73
    SYSCALL("sethostname", kHex, kHex, kHex, kHex, kHex, kHex),         // 74
    SYSCALL("setrlimit", kHex, kHex, kHex, kHex, kHex, kHex),           // 75
    SYSCALL("getrlimit", kHex, kHex, kHex, kHex, kHex, kHex),           // 76
    SYSCALL("getrusage", kHex, kHex, kHex, kHex, kHex, kHex),           // 77
    SYSCALL("gettimeofday", kHex, kHex, kHex, kHex, kHex, kHex),        // 78
    SYSCALL("settimeofday", kHex, kHex, kHex, kHex, kHex, kHex),        // 79
    SYSCALL("getgroups", kHex, kHex, kHex, kHex, kHex, kHex),           // 80
    SYSCALL("setgroups", kHex, kHex, kHex, kHex, kHex, kHex),           // 81
    SYSCALL("select", kHex, kHex, kHex, kHex, kHex, kHex),              // 82
    SYSCALL("symlink", kPath, kPath, kHex, kHex, kHex, kHex),           // 83
    SYSCALL("oldlstat", kHex, kHex, kHex, kHex, kHex, kHex),            // 84
    SYSCALL("readlink", kPath, kHex, kInt, kHex, kHex, kHex),           // 85
    SYSCALL("uselib", kPath, kHex, kHex, kHex, kHex, kHex),             // 86
    SYSCALL("swapon", kHex, kHex, kHex, kHex, kHex, kHex),              // 87
    SYSCALL("reboot", kHex, kHex, kHex, kHex, kHex, kHex),              // 88
    SYSCALL("readdir", kHex, kHex, kHex, kHex, kHex, kHex),             // 89
    SYSCALL("mmap", kHex, kHex, kHex, kHex, kHex, kHex),                // 90
    SYSCALL("munmap", kHex, kHex, kHex, kHex, kHex, kHex),              // 91
    SYSCALL("truncate", kPath, kHex, kHex, kHex, kHex, kHex),           // 92
    SYSCALL("ftruncate", kHex, kHex, kHex, kHex, kHex, kHex),           // 93
    SYSCALL("fchmod", kHex, kHex, kHex, kHex, kHex, kHex),              // 94
    SYSCALL("fchown", kHex, kHex, kHex, kHex, kHex, kHex),              // 95
    SYSCALL("getpriority", kHex, kHex, kHex, kHex, kHex, kHex),         // 96
    SYSCALL("setpriority", kHex, kHex, kHex, kHex, kHex, kHex),         // 97
    SYSCALL("profil", kHex, kHex, kHex, kHex, kHex, kHex),              // 98
    SYSCALL("statfs", kPath, kHex, kHex, kHex, kHex, kHex),             // 99
    SYSCALL("fstatfs", kHex, kHex, kHex, kHex, kHex, kHex),             // 100
    SYSCALL("ioperm", kHex, kHex, kHex, kHex, kHex, kHex),              // 101
    SYSCALL("socketcall", kHex, kHex, kHex, kHex, kHex, kHex),          // 102
    SYSCALL("syslog", kHex, kHex, kHex, kHex, kHex, kHex),              // 103
    SYSCALL("setitimer", kHex, kHex, kHex, kHex, kHex, kHex),           // 104
    SYSCALL("getitimer", kHex, kHex, kHex, kHex, kHex, kHex),           // 105
    SYSCALL("stat", kPath, kHex, kHex, kHex, kHex, kHex),               // 106
    SYSCALL("lstat", kPath, kHex, kHex, kHex, kHex, kHex),              // 107
    SYSCALL("fstat", kHex, kHex, kHex, kHex, kHex, kHex),               // 108
    SYSCALL("olduname", kHex, kHex, kHex, kHex, kHex, kHex),            // 109
    SYSCALL("iopl", kHex, kHex, kHex, kHex, kHex, kHex),                // 110
    SYSCALL("vhangup", kHex, kHex, kHex, kHex, kHex, kHex),             // 111
    SYSCALL("idle", kHex, kHex, kHex, kHex, kHex, kHex),                // 112
    SYSCALL("vm86old", kHex, kHex, kHex, kHex, kHex, kHex),             // 113
    SYSCALL("wait4", kHex, kHex, kHex, kHex, kHex, kHex),               // 114
    SYSCALL("swapoff", kHex, kHex, kHex, kHex, kHex, kHex),             // 115
    SYSCALL("sysinfo", kHex, kHex, kHex, kHex, kHex, kHex),             // 116
    SYSCALL("ipc", kHex, kHex, kHex, kHex, kHex, kHex),                 // 117
    SYSCALL("fsync", kHex, kHex, kHex, kHex, kHex, kHex),               // 118
    SYSCALL("sigreturn", kHex, kHex, kHex, kHex, kHex, kHex),           // 119
    SYSCALL("clone", kHex, kHex, kHex, kHex, kHex, kHex),               // 120
    SYSCALL("setdomainname", kHex, kHex, kHex, kHex, kHex, kHex),       // 121
    SYSCALL("uname", kHex, kHex, kHex, kHex, kHex, kHex),               // 122
    SYSCALL("modify_ldt", kHex, kHex, kHex, kHex, kHex, kHex),          // 123
    SYSCALL("adjtimex", kHex, kHex, kHex, kHex, kHex, kHex),            // 124
    SYSCALL("mprotect", kHex, kHex, kHex, kHex, kHex, kHex),            // 125
    SYSCALL("sigprocmask", kHex, kHex, kHex, kHex, kHex, kHex),         // 126
    SYSCALL("create_module", kHex, kHex, kHex, kHex, kHex, kHex),       // 127
    SYSCALL("init_module", kHex, kHex, kHex, kHex, kHex, kHex),         // 128
    SYSCALL("delete_module", kHex, kHex, kHex, kHex, kHex, kHex),       // 129
    SYSCALL("get_kernel_syms", kHex, kHex, kHex, kHex, kHex, kHex),     // 130
    SYSCALL("quotactl", kHex, kHex, kHex, kHex, kHex, kHex),            // 131
    SYSCALL("getpgid", kHex, kHex, kHex, kHex, kHex, kHex),             // 132
    SYSCALL("fchdir", kHex, kHex, kHex, kHex, kHex, kHex),              // 133
    SYSCALL("bdflush", kHex, kHex, kHex, kHex, kHex, kHex),             // 134
    SYSCALL("sysfs", kHex, kHex, kHex, kHex, kHex, kHex),               // 135
    SYSCALL("personality", kHex, kHex, kHex, kHex, kHex, kHex),         // 136
    SYSCALL("afs_syscall", kHex, kHex, kHex, kHex, kHex, kHex),         // 137
    SYSCALL("setfsuid", kHex, kHex, kHex, kHex, kHex, kHex),            // 138
    SYSCALL("setfsgid", kHex, kHex, kHex, kHex, kHex, kHex),            // 139
    SYSCALL("_llseek", kHex, kHex, kHex, kHex, kHex, kHex),             // 140
    SYSCALL("getdents", kHex, kHex, kHex, kHex, kHex, kHex),            // 141
    SYSCALL("_newselect", kHex, kHex, kHex, kHex, kHex, kHex),          // 142
    SYSCALL("flock", kHex, kHex, kHex, kHex, kHex, kHex),               // 143
    SYSCALL("msync", kHex, kHex, kHex, kHex, kHex, kHex),               // 144
    SYSCALL("readv", kHex, kHex, kHex, kHex, kHex, kHex),               // 145
    SYSCALL("writev", kHex, kHex, kHex, kHex, kHex, kHex),              // 146
    SYSCALL("getsid", kHex, kHex, kHex, kHex, kHex, kHex),              // 147
    SYSCALL("fdatasync", kHex, kHex, kHex, kHex, kHex, kHex),           // 148
    SYSCALL("_sysctl", kHex, kHex, kHex, kHex, kHex, kHex),             // 149
    SYSCALL("mlock", kHex, kHex, kHex, kHex, kHex, kHex),               // 150
    SYSCALL("munlock", kHex, kHex, kHex, kHex, kHex, kHex),             // 151
    SYSCALL("mlockall", kHex, kHex, kHex, kHex, kHex, kHex),            // 152
    SYSCALL("munlockall", kHex, kHex, kHex, kHex, kHex, kHex),          // 153
    SYSCALL("sched_setparam", kHex, kHex, kHex, kHex, kHex, kHex),      // 154
    SYSCALL("sched_getparam", kHex, kHex, kHex, kHex, kHex, kHex),      // 155
    SYSCALL("sched_setscheduler", kHex, kHex, kHex, kHex, kHex, kHex),  // 156
    SYSCALL("sched_getscheduler", kHex, kHex, kHex, kHex, kHex, kHex),  // 157
    SYSCALL("sched_yield", kHex, kHex, kHex, kHex, kHex, kHex),         // 158
    SYSCALL("sched_get_priority_max", kHex, kHex, kHex, kHex, kHex,
            kHex),  // 159
    SYSCALL("sched_get_priority_min", kHex, kHex, kHex, kHex, kHex,
            kHex),  // 160
    SYSCALL("sched_rr_get_interval", kHex, kHex, kHex, kHex, kHex,
            kHex),                                                     // 161
    SYSCALL("nanosleep", kHex, kHex, kHex, kHex, kHex, kHex),          // 162
    SYSCALL("mremap", kHex, kHex, kHex, kHex, kHex, kHex),             // 163
    SYSCALL("setresuid", kHex, kHex, kHex, kHex, kHex, kHex),          // 164
    SYSCALL("getresuid", kHex, kHex, kHex, kHex, kHex, kHex),          // 165
    SYSCALL("vm86", kHex, kHex, kHex, kHex, kHex, kHex),               // 166
    SYSCALL("query_module", kHex, kHex, kHex, kHex, kHex, kHex),       // 167
    SYSCALL("poll", kHex, kHex, kHex, kHex, kHex, kHex),               // 168
    SYSCALL("nfsservctl", kHex, kHex, kHex, kHex, kHex, kHex),         // 169
    SYSCALL("setresgid", kHex, kHex, kHex, kHex, kHex, kHex),          // 170
    SYSCALL("getresgid", kHex, kHex, kHex, kHex, kHex, kHex),          // 171
    SYSCALL("prctl", kHex, kHex, kHex, kHex, kHex, kHex),              // 172
    SYSCALL("rt_sigreturn", kHex, kHex, kHex, kHex, kHex, kHex),       // 173
    SYSCALL("rt_sigaction", kHex, kHex, kHex, kHex, kHex, kHex),       // 174
    SYSCALL("rt_sigprocmask", kHex, kHex, kHex, kHex, kHex, kHex),     // 175
    SYSCALL("rt_sigpending", kHex, kHex, kHex, kHex, kHex, kHex),      // 176
    SYSCALL("rt_sigtimedwait", kHex, kHex, kHex, kHex, kHex, kHex),    // 177
    SYSCALL("rt_sigqueueinfo", kHex, kHex, kHex, kHex, kHex, kHex),    // 178
    SYSCALL("rt_sigsuspend", kHex, kHex, kHex, kHex, kHex, kHex),      // 179
    SYSCALL("pread64", kHex, kHex, kHex, kHex, kHex, kHex),            // 180
    SYSCALL("pwrite64", kHex, kHex, kHex, kHex, kHex, kHex),           // 181
    SYSCALL("chown", kHex, kHex, kHex, kHex, kHex, kHex),              // 182
    SYSCALL("getcwd", kHex, kHex, kHex, kHex, kHex, kHex),             // 183
    SYSCALL("capget", kHex, kHex, kHex, kHex, kHex, kHex),             // 184
    SYSCALL("capset", kHex, kHex, kHex, kHex, kHex, kHex),             // 185
    SYSCALL("sigaltstack", kHex, kHex, kHex, kHex, kHex, kHex),        // 186
    SYSCALL("sendfile", kHex, kHex, kHex, kHex, kHex, kHex),           // 187
    SYSCALL("getpmsg", kHex, kHex, kHex, kHex, kHex, kHex),            // 188
    SYSCALL("putpmsg", kHex, kHex, kHex, kHex, kHex, kHex),            // 189
    SYSCALL("vfork", kHex, kHex, kHex, kHex, kHex, kHex),              // 190
    SYSCALL("ugetrlimit", kHex, kHex, kHex, kHex, kHex, kHex),         // 191
    SYSCALL("mmap2", kHex, kHex, kHex, kHex, kHex, kHex),              // 192
    SYSCALL("truncate64", kPath, kHex, kHex, kHex, kHex, kHex),        // 193
    SYSCALL("ftruncate64", kHex, kHex, kHex, kHex, kHex, kHex),        // 194
    SYSCALL("stat64", kHex, kHex, kHex, kHex, kHex, kHex),             // 195
    SYSCALL("lstat64", kPath, kHex, kHex, kHex, kHex, kHex),           // 196
    SYSCALL("fstat64", kHex, kHex, kHex, kHex, kHex, kHex),            // 197
    SYSCALL("lchown32", kHex, kHex, kHex, kHex, kHex, kHex),           // 198
    SYSCALL("getuid32", kHex, kHex, kHex, kHex, kHex, kHex),           // 199
    SYSCALL("getgid32", kHex, kHex, kHex, kHex, kHex, kHex),           // 200
    SYSCALL("geteuid32", kHex, kHex, kHex, kHex, kHex, kHex),          // 201
    SYSCALL("getegid32", kHex, kHex, kHex, kHex, kHex, kHex),          // 202
    SYSCALL("setreuid32", kHex, kHex, kHex, kHex, kHex, kHex),         // 203
    SYSCALL("setregid32", kHex, kHex, kHex, kHex, kHex, kHex),         // 204
    SYSCALL("getgroups32", kHex, kHex, kHex, kHex, kHex, kHex),        // 205
    SYSCALL("setgroups32", kHex, kHex, kHex, kHex, kHex, kHex),        // 206
    SYSCALL("fchown32", kHex, kHex, kHex, kHex, kHex, kHex),           // 207
    SYSCALL("setresuid32", kHex, kHex, kHex, kHex, kHex, kHex),        // 208
    SYSCALL("getresuid32", kHex, kHex, kHex, kHex, kHex, kHex),        // 209
    SYSCALL("setresgid32", kHex, kHex, kHex, kHex, kHex, kHex),        // 210
    SYSCALL("getresgid32", kHex, kHex, kHex, kHex, kHex, kHex),        // 211
    SYSCALL("chown32", kHex, kHex, kHex, kHex, kHex, kHex),            // 212
    SYSCALL("setuid32", kHex, kHex, kHex, kHex, kHex, kHex),           // 213
    SYSCALL("setgid32", kHex, kHex, kHex, kHex, kHex, kHex),           // 214
    SYSCALL("setfsuid32", kHex, kHex, kHex, kHex, kHex, kHex),         // 215
    SYSCALL("setfsgid32", kHex, kHex, kHex, kHex, kHex, kHex),         // 216
    SYSCALL("pivot_root", kHex, kHex, kHex, kHex, kHex, kHex),         // 217
    SYSCALL("mincore", kHex, kHex, kHex, kHex, kHex, kHex),            // 218
    SYSCALL("madvise", kHex, kHex, kHex, kHex, kHex, kHex),            // 219
    SYSCALL("getdents64", kHex, kHex, kHex, kHex, kHex, kHex),         // 220
    SYSCALL("fcntl64", kHex, kHex, kHex, kHex, kHex, kHex),            // 221
    SYSCALL("unused1-222", kHex, kHex, kHex, kHex, kHex, kHex),        // 222
    SYSCALL("unused2-223", kHex, kHex, kHex, kHex, kHex, kHex),        // 223
    SYSCALL("gettid", kHex, kHex, kHex, kHex, kHex, kHex),             // 224
    SYSCALL("readahead", kHex, kHex, kHex, kHex, kHex, kHex),          // 225
    SYSCALL("setxattr", kHex, kHex, kHex, kHex, kHex, kHex),           // 226
    SYSCALL("lsetxattr", kHex, kHex, kHex, kHex, kHex, kHex),          // 227
    SYSCALL("fsetxattr", kHex, kHex, kHex, kHex, kHex, kHex),          // 228
    SYSCALL("getxattr", kHex, kHex, kHex, kHex, kHex, kHex),           // 229
    SYSCALL("lgetxattr", kHex, kHex, kHex, kHex, kHex, kHex),          // 230
    SYSCALL("fgetxattr", kHex, kHex, kHex, kHex, kHex, kHex),          // 231
    SYSCALL("listxattr", kHex, kHex, kHex, kHex, kHex, kHex),          // 232
    SYSCALL("llistxattr", kHex, kHex, kHex, kHex, kHex, kHex),         // 233
    SYSCALL("flistxattr", kHex, kHex, kHex, kHex, kHex, kHex),         // 234
    SYSCALL("removexattr", kHex, kHex, kHex, kHex, kHex, kHex),        // 235
    SYSCALL("lremovexattr", kHex, kHex, kHex, kHex, kHex, kHex),       // 236
    SYSCALL("fremovexattr", kHex, kHex, kHex, kHex, kHex, kHex),       // 237
    SYSCALL("tkill", kHex, kHex, kHex, kHex, kHex, kHex),              // 238
    SYSCALL("sendfile64", kHex, kHex, kHex, kHex, kHex, kHex),         // 239
    SYSCALL("futex", kHex, kHex, kHex, kHex, kHex, kHex),              // 240
    SYSCALL("sched_setaffinity", kHex, kHex, kHex, kHex, kHex, kHex),  // 241
    SYSCALL("sched_getaffinity", kHex, kHex, kHex, kHex, kHex, kHex),  // 242
    SYSCALL("set_thread_area", kHex, kHex, kHex, kHex, kHex, kHex),    // 243
    SYSCALL("get_thread_area", kHex, kHex, kHex, kHex, kHex, kHex),    // 244
    SYSCALL("io_setup", kHex, kHex, kHex, kHex, kHex, kHex),           // 245
    SYSCALL("io_destroy", kHex, kHex, kHex, kHex, kHex, kHex),         // 246
    SYSCALL("io_getevents", kHex, kHex, kHex, kHex, kHex, kHex),       // 247
    SYSCALL("io_submit", kHex, kHex, kHex, kHex, kHex, kHex),          // 248
    SYSCALL("io_cancel", kHex, kHex, kHex, kHex, kHex, kHex),          // 249
    SYSCALL("fadvise64", kHex, kHex, kHex, kHex, kHex, kHex),          // 250
    SYSCALL("251-old_sys_set_zone_reclaim", kHex, kHex, kHex, kHex, kHex,
            kHex),                                                    // 251
    SYSCALL("exit_group", kHex, kHex, kHex, kHex, kHex, kHex),        // 252
    SYSCALL("lookup_dcookie", kHex, kHex, kHex, kHex, kHex, kHex),    // 253
    SYSCALL("epoll_create", kHex, kHex, kHex, kHex, kHex, kHex),      // 254
    SYSCALL("epoll_ctl", kHex, kHex, kHex, kHex, kHex, kHex),         // 255
    SYSCALL("epoll_wait", kHex, kHex, kHex, kHex, kHex, kHex),        // 256
    SYSCALL("remap_file_pages", kHex, kHex, kHex, kHex, kHex, kHex),  // 257
    SYSCALL("set_tid_address", kHex, kHex, kHex, kHex, kHex, kHex),   // 258
    SYSCALL("timer_create", kHex, kHex, kHex, kHex, kHex, kHex),      // 259
    SYSCALL("timer_settime", kHex, kHex, kHex, kHex, kHex, kHex),     // 260
    SYSCALL("timer_gettime", kHex, kHex, kHex, kHex, kHex, kHex),     // 261
    SYSCALL("timer_getoverrun", kHex, kHex, kHex, kHex, kHex, kHex),  // 262
    SYSCALL("timer_delete", kHex, kHex, kHex, kHex, kHex, kHex),      // 263
    SYSCALL("clock_settime", kHex, kHex, kHex, kHex, kHex, kHex),     // 264
    SYSCALL("clock_gettime", kHex, kHex, kHex, kHex, kHex, kHex),     // 265
    SYSCALL("clock_getres", kHex, kHex, kHex, kHex, kHex, kHex),      // 266
    SYSCALL("clock_nanosleep", kHex, kHex, kHex, kHex, kHex, kHex),   // 267
    SYSCALL("statfs64", kHex, kHex, kHex, kHex, kHex, kHex),          // 268
    SYSCALL("fstatfs64", kHex, kHex, kHex, kHex, kHex, kHex),         // 269
    SYSCALL("tgkill", kHex, kHex, kHex, kHex, kHex, kHex),            // 270
    SYSCALL("utimes", kHex, kHex, kHex, kHex, kHex, kHex),            // 271
    SYSCALL("fadvise64_64", kHex, kHex, kHex, kHex, kHex, kHex),      // 272
    SYSCALL("vserver", kHex, kHex, kHex, kHex, kHex, kHex),           // 273
    SYSCALL("mbind", kHex, kHex, kHex, kHex, kHex, kHex),             // 274
    SYSCALL("get_mempolicy", kHex, kHex, kHex, kHex, kHex, kHex),     // 275
    SYSCALL("set_mempolicy", kHex, kHex, kHex, kHex, kHex, kHex),     // 276
    SYSCALL("mq_open", kHex, kHex, kHex, kHex, kHex, kHex),           // 277
    SYSCALL("mq_unlink", kHex, kHex, kHex, kHex, kHex, kHex),         // 278
    SYSCALL("mq_timedsend", kHex, kHex, kHex, kHex, kHex, kHex),      // 279
    SYSCALL("mq_timedreceive", kHex, kHex, kHex, kHex, kHex, kHex),   // 280
    SYSCALL("mq_notify", kHex, kHex, kHex, kHex, kHex, kHex),         // 281
    SYSCALL("mq_getsetattr", kHex, kHex, kHex, kHex, kHex, kHex),     // 282
    SYSCALL("kexec_load", kHex, kHex, kHex, kHex, kHex, kHex),        // 283
    SYSCALL("waitid", kHex, kHex, kHex, kHex, kHex, kHex),            // 284
    SYSCALL("285-old_sys_setaltroot", kHex, kHex, kHex, kHex, kHex,
            kHex),                                                     // 285
    SYSCALL("add_key", kHex, kHex, kHex, kHex, kHex, kHex),            // 286
    SYSCALL("request_key", kHex, kHex, kHex, kHex, kHex, kHex),        // 287
    SYSCALL("keyctl", kHex, kHex, kHex, kHex, kHex, kHex),             // 288
    SYSCALL("ioprio_set", kHex, kHex, kHex, kHex, kHex, kHex),         // 289
    SYSCALL("ioprio_get", kHex, kHex, kHex, kHex, kHex, kHex),         // 290
    SYSCALL("inotify_init", kHex, kHex, kHex, kHex, kHex, kHex),       // 291
    SYSCALL("inotify_add_watch", kHex, kHex, kHex, kHex, kHex, kHex),  // 292
    SYSCALL("inotify_rm_watch", kHex, kHex, kHex, kHex, kHex, kHex),   // 293
    SYSCALL("migrate_pages", kHex, kHex, kHex, kHex, kHex, kHex),      // 294
    SYSCALL("openat", kHex, kPath, kOct, kHex, kHex, kHex),            // 295
    SYSCALL("mkdirat", kHex, kHex, kHex, kHex, kHex, kHex),            // 296
    SYSCALL("mknodat", kHex, kHex, kHex, kHex, kHex, kHex),            // 297
    SYSCALL("fchownat", kHex, kPath, kHex, kHex, kHex, kHex),          // 298
    SYSCALL("futimesat", kHex, kPath, kHex, kHex, kHex, kHex),         // 299
    SYSCALL("fstatat64", kHex, kHex, kHex, kHex, kHex, kHex),          // 300
    SYSCALL("unlinkat", kHex, kPath, kHex, kHex, kHex, kHex),          // 301
    SYSCALL("renameat", kHex, kPath, kHex, kPath, kHex, kHex),         // 302
    SYSCALL("linkat", kHex, kPath, kHex, kPath, kHex, kHex),           // 303
    SYSCALL("symlinkat", kPath, kHex, kPath, kHex, kHex, kHex),        // 304
    SYSCALL("readlinkat", kHex, kPath, kHex, kHex, kHex, kHex),        // 305
    SYSCALL("fchmodat", kHex, kPath, kHex, kHex, kHex, kHex),          // 306
    SYSCALL("faccessat", kHex, kPath, kHex, kHex, kHex, kHex),         // 307
    SYSCALL("pselect6", kHex, kHex, kHex, kHex, kHex, kHex),           // 308
    SYSCALL("ppoll", kHex, kHex, kHex, kHex, kHex, kHex),              // 309
    SYSCALL("unshare", kHex, kHex, kHex, kHex, kHex, kHex),            // 310
    SYSCALL("set_robust_list", kHex, kHex, kHex, kHex, kHex, kHex),    // 311
    SYSCALL("get_robust_list", kHex, kHex, kHex, kHex, kHex, kHex),    // 312
    SYSCALL("splice", kHex, kHex, kHex, kHex, kHex, kHex),             // 313
    SYSCALL("sync_file_range", kHex, kHex, kHex, kHex, kHex, kHex),    // 314
    SYSCALL("tee", kHex, kHex, kHex, kHex, kHex, kHex),                // 315
    SYSCALL("vmsplice", kHex, kHex, kHex, kHex, kHex, kHex),           // 316
    SYSCALL("move_pages", kHex, kHex, kHex, kHex, kHex, kHex),         // 317
    SYSCALL("getcpu", kHex, kHex, kHex, kHex, kHex, kHex),             // 318
    SYSCALL("epoll_pwait", kHex, kHex, kHex, kHex, kHex, kHex),        // 319
    SYSCALL("utimensat", kHex, kHex, kHex, kHex, kHex, kHex),          // 320
    SYSCALL("signalfd", kHex, kHex, kHex, kHex, kHex, kHex),           // 321
    SYSCALL("timerfd_create", kHex, kHex, kHex, kHex, kHex, kHex),     // 322
    SYSCALL("eventfd", kHex, kHex, kHex, kHex, kHex, kHex),            // 323
    SYSCALL("fallocate", kHex, kHex, kHex, kHex, kHex, kHex),          // 324
    SYSCALL("timerfd_settime", kHex, kHex, kHex, kHex, kHex, kHex),    // 325
    SYSCALL("timerfd_gettime", kHex, kHex, kHex, kHex, kHex, kHex),    // 326
    SYSCALL("signalfd4", kHex, kHex, kHex, kHex, kHex, kHex),          // 327
    SYSCALL("eventfd2", kHex, kHex, kHex, kHex, kHex, kHex),           // 328
    SYSCALL("epoll_create1", kHex, kHex, kHex, kHex, kHex, kHex),      // 329
    SYSCALL("dup3", kHex, kHex, kHex, kHex, kHex, kHex),               // 330
    SYSCALL("pipe2", kHex, kHex, kHex, kHex, kHex, kHex),              // 331
    SYSCALL("inotify_init1", kHex, kHex, kHex, kHex, kHex, kHex),      // 332
    SYSCALL("preadv", kHex, kHex, kHex, kHex, kHex, kHex),             // 333
    SYSCALL("pwritev", kHex, kHex, kHex, kHex, kHex, kHex),            // 334
    SYSCALL("rt_tgsigqueueinfo", kHex, kHex, kHex, kHex, kHex, kHex),  // 335
    SYSCALL("perf_event_open", kHex, kHex, kHex, kHex, kHex, kHex),    // 336
    SYSCALL("recvmmsg", kHex, kHex, kHex, kHex, kHex, kHex),           // 337
    SYSCALL("fanotify_init", kHex, kHex, kHex, kHex, kHex, kHex),      // 338
    SYSCALL("fanotify_mark", kHex, kHex, kHex, kHex, kHex, kHex),      // 339
    SYSCALL("prlimit64", kHex, kHex, kHex, kHex, kHex, kHex),          // 340
    SYSCALL("name_to_handle_at", kHex, kHex, kHex, kHex, kHex, kHex),  // 341
    SYSCALL("open_by_handle_at", kHex, kHex, kHex, kHex, kHex, kHex),  // 342
    SYSCALL("clock_adjtime", kHex, kHex, kHex, kHex, kHex, kHex),      // 343
    SYSCALL("syncfs", kHex, kHex, kHex, kHex, kHex, kHex),             // 344
    SYSCALL("sendmmsg", kHex, kHex, kHex, kHex, kHex, kHex),           // 345
    SYSCALL("setns", kHex, kHex, kHex, kHex, kHex, kHex),              // 346
    SYSCALL("process_vm_readv", kHex, kHex, kHex, kHex, kHex, kHex),   // 347
    SYSCALL("process_vm_writev", kHex, kHex, kHex, kHex, kHex, kHex),  // 348
    SYSCALL("kcmp", kHex, kHex, kHex, kHex, kHex, kHex),               // 349
    SYSCALL("finit_module", kHex, kHex, kHex, kHex, kHex, kHex),       // 350
    SYSCALL("sched_setattr", kHex, kHex, kHex, kHex, kHex, kHex),      // 351
    SYSCALL("sched_getattr", kHex, kHex, kHex, kHex, kHex, kHex),      // 352
    SYSCALL("renameat2", kHex, kPath, kHex, kPath, kHex, kHex),        // 353
    SYSCALL("seccomp", kHex, kHex, kHex, kHex, kHex, kHex),            // 354
    SYSCALL("getrandom", kHex, kHex, kHex, kHex, kHex, kHex),          // 355
    SYSCALL("memfd_create", kHex, kHex, kHex, kHex, kHex, kHex),       // 356
    SYSCALL("bpf", kHex, kHex, kHex, kHex, kHex, kHex),                // 357
};

#elif defined(__powerpc64__)

// http://lxr.free-electrons.com/source/arch/powerpc/include/uapi/asm/unistd.h
// Note: PPC64 syscalls can have up to 7 register arguments, but nobody is
// using the 7th argument - probably for x64 compatibility reasons.
const absl::Span<const SyscallTable::Entry> SyscallTable::kSyscallDataPPC64 = {
    SYSCALL("restart_syscall", kGen, kGen, kGen, kGen, kGen, kGen),     // 0
    SYSCALL("exit", kInt, kGen, kGen, kGen, kGen, kGen),                // 1
    SYSCALL("fork", kGen, kGen, kGen, kGen, kGen, kGen),                // 2
    SYSCALL("read", kInt, kHex, kInt),                                  // 3
    SYSCALL("write", kInt, kHex, kInt, kGen, kGen, kGen),               // 4
    SYSCALL("open", kPath, kHex, kOct, kGen, kGen, kGen),               // 5
    SYSCALL("close", kInt, kGen, kGen, kGen, kGen, kGen),               // 6
    SYSCALL("waitpid", kHex, kHex, kHex, kHex, kHex, kHex),             // 7
    SYSCALL("creat", kPath, kOct, kGen, kGen, kGen, kGen),              // 8
    SYSCALL("link", kPath, kPath, kGen, kGen, kGen, kGen),              // 9
    SYSCALL("unlink", kPath, kGen, kGen, kGen, kGen, kGen),             // 10
    SYSCALL("execve", kPath, kHex, kHex, kGen, kGen, kGen),             // 11
    SYSCALL("chdir", kPath, kGen, kGen, kGen, kGen, kGen),              // 12
    SYSCALL("time", kHex, kGen, kGen, kGen, kGen, kGen),                // 13
    SYSCALL("mknod", kPath, kOct, kHex, kGen, kGen, kGen),              // 14
    SYSCALL("chmod", kPath, kOct, kGen, kGen, kGen, kGen),              // 15
    SYSCALL("lchown", kPath, kInt, kInt, kGen, kGen, kGen),             // 16
    SYSCALL("break", kHex, kHex, kHex, kHex, kHex, kHex),               // 17
    SYSCALL("oldstat", kHex, kHex, kHex, kHex, kHex, kHex),             // 18
    SYSCALL("lseek", kGen, kGen, kGen, kGen, kGen, kGen),               // 19
    SYSCALL("getpid", kGen, kGen, kGen, kGen, kGen, kGen),              // 20
    SYSCALL("mount", kPath, kPath, kString, kHex, kGen, kGen),          // 21
    SYSCALL("umount", kHex, kHex, kHex, kHex, kHex, kHex),              // 22
    SYSCALL("setuid", kGen, kGen, kGen, kGen, kGen, kGen),              // 23
    SYSCALL("getuid", kGen, kGen, kGen, kGen, kGen, kGen),              // 24
    SYSCALL("stime", kHex, kHex, kHex, kHex, kHex, kHex),               // 25
    SYSCALL("ptrace", kGen, kGen, kGen, kGen, kGen, kGen),              // 26
    SYSCALL("alarm", kInt, kGen, kGen, kGen, kGen, kGen),               // 27
    SYSCALL("oldfstat", kHex, kHex, kHex, kHex, kHex, kHex),            // 28
    SYSCALL("pause", kGen, kGen, kGen, kGen, kGen, kGen),               // 29
    SYSCALL("utime", kGen, kGen, kGen, kGen, kGen, kGen),               // 30
    SYSCALL("stty", kHex, kHex, kHex, kHex, kHex, kHex),                // 31
    SYSCALL("gtty", kHex, kHex, kHex, kHex, kHex, kHex),                // 32
    SYSCALL("access", kPath, kHex, kGen, kGen, kGen, kGen),             // 33
    SYSCALL("nice", kHex, kHex, kHex, kHex, kHex, kHex),                // 34
    SYSCALL("ftime", kHex, kHex, kHex, kHex, kHex, kHex),               // 35
    SYSCALL("sync", kGen, kGen, kGen, kGen, kGen, kGen),                // 36
    SYSCALL("kill", kInt, kSignal, kGen, kGen, kGen, kGen),             // 37
    SYSCALL("rename", kPath, kPath, kGen, kGen, kGen, kGen),            // 38
    SYSCALL("mkdir", kPath, kOct, kGen, kGen, kGen, kGen),              // 39
    SYSCALL("rmdir", kPath, kGen, kGen, kGen, kGen, kGen),              // 40
    SYSCALL("dup", kGen, kGen, kGen, kGen, kGen, kGen),                 // 41
    SYSCALL("pipe", kGen, kGen, kGen, kGen, kGen, kGen),                // 42
    SYSCALL("times", kGen, kGen, kGen, kGen, kGen, kGen),               // 43
    SYSCALL("prof", kHex, kHex, kHex, kHex, kHex, kHex),                // 44
    SYSCALL("brk", kHex, kGen, kGen, kGen, kGen, kGen),                 // 45
    SYSCALL("setgid", kGen, kGen, kGen, kGen, kGen, kGen),              // 46
    SYSCALL("getgid", kGen, kGen, kGen, kGen, kGen, kGen),              // 47
    SYSCALL("signal", kHex, kHex, kHex, kHex, kHex, kHex),              // 48
    SYSCALL("geteuid", kGen, kGen, kGen, kGen, kGen, kGen),             // 49
    SYSCALL("getegid", kGen, kGen, kGen, kGen, kGen, kGen),             // 50
    SYSCALL("acct", kPath, kGen, kGen, kGen, kGen, kGen),               // 51
    SYSCALL("umount2", kPath, kHex, kGen, kGen, kGen, kGen),            // 52
    SYSCALL("lock", kHex, kHex, kHex, kHex, kHex, kHex),                // 53
    SYSCALL("ioctl", kGen, kGen, kGen, kGen, kGen, kGen),               // 54
    SYSCALL("fcntl", kGen, kGen, kGen, kGen, kGen, kGen),               // 55
    SYSCALL("mpx", kHex, kHex, kHex, kHex, kHex, kHex),                 // 56
    SYSCALL("setpgid", kGen, kGen, kGen, kGen, kGen, kGen),             // 57
    SYSCALL("ulimit", kHex, kHex, kHex, kHex, kHex, kHex),              // 58
    SYSCALL("oldolduname", kHex, kHex, kHex, kHex, kHex, kHex),         // 59
    SYSCALL("umask", kHex, kGen, kGen, kGen, kGen, kGen),               // 60
    SYSCALL("chroot", kPath, kGen, kGen, kGen, kGen, kGen),             // 61
    SYSCALL("ustat", kGen, kGen, kGen, kGen, kGen, kGen),               // 62
    SYSCALL("dup2", kGen, kGen, kGen, kGen, kGen, kGen),                // 63
    SYSCALL("getppid", kGen, kGen, kGen, kGen, kGen, kGen),             // 64
    SYSCALL("getpgrp", kGen, kGen, kGen, kGen, kGen, kGen),             // 65
    SYSCALL("setsid", kGen, kGen, kGen, kGen, kGen, kGen),              // 66
    SYSCALL("sigaction", kHex, kHex, kHex, kHex, kHex, kHex),           // 67
    SYSCALL("sgetmask", kHex, kHex, kHex, kHex, kHex, kHex),            // 68
    SYSCALL("ssetmask", kHex, kHex, kHex, kHex, kHex, kHex),            // 69
    SYSCALL("setreuid", kGen, kGen, kGen, kGen, kGen, kGen),            // 70
    SYSCALL("setregid", kGen, kGen, kGen, kGen, kGen, kGen),            // 71
    SYSCALL("sigsuspend", kHex, kHex, kHex, kHex, kHex, kHex),          // 72
    SYSCALL("sigpending", kHex, kHex, kHex, kHex, kHex, kHex),          // 73
    SYSCALL("sethostname", kGen, kGen, kGen, kGen, kGen, kGen),         // 74
    SYSCALL("setrlimit", kGen, kGen, kGen, kGen, kGen, kGen),           // 75
    SYSCALL("getrlimit", kGen, kGen, kGen, kGen, kGen, kGen),           // 76
    SYSCALL("getrusage", kGen, kGen, kGen, kGen, kGen, kGen),           // 77
    SYSCALL("gettimeofday", kHex, kHex, kGen, kGen, kGen, kGen),        // 78
    SYSCALL("settimeofday", kHex, kHex, kGen, kGen, kGen, kGen),        // 79
    SYSCALL("getgroups", kGen, kGen, kGen, kGen, kGen, kGen),           // 80
    SYSCALL("setgroups", kGen, kGen, kGen, kGen, kGen, kGen),           // 81
    SYSCALL("select", kGen, kGen, kGen, kGen, kGen, kGen),              // 82
    SYSCALL("symlink", kPath, kPath, kGen, kGen, kGen, kGen),           // 83
    SYSCALL("oldlstat", kHex, kHex, kHex, kHex, kHex, kHex),            // 84
    SYSCALL("readlink", kPath, kGen, kInt, kGen, kGen, kGen),           // 85
    SYSCALL("uselib", kPath, kGen, kGen, kGen, kGen, kGen),             // 86
    SYSCALL("swapon", kPath, kHex, kGen, kGen, kGen, kGen),             // 87
    SYSCALL("reboot", kGen, kGen, kGen, kGen, kGen, kGen),              // 88
    SYSCALL("readdir", kHex, kHex, kHex, kHex, kHex, kHex),             // 89
    SYSCALL("mmap", kHex, kInt, kHex, kHex, kInt, kInt),                // 90
    SYSCALL("munmap", kHex, kHex, kGen, kGen, kGen, kGen),              // 91
    SYSCALL("truncate", kPath, kInt, kGen, kGen, kGen, kGen),           // 92
    SYSCALL("ftruncate", kGen, kGen, kGen, kGen, kGen, kGen),           // 93
    SYSCALL("fchmod", kGen, kGen, kGen, kGen, kGen, kGen),              // 94
    SYSCALL("fchown", kGen, kGen, kGen, kGen, kGen, kGen),              // 95
    SYSCALL("getpriority", kGen, kGen, kGen, kGen, kGen, kGen),         // 96
    SYSCALL("setpriority", kGen, kGen, kGen, kGen, kGen, kGen),         // 97
    SYSCALL("profil", kHex, kHex, kHex, kHex, kHex, kHex),              // 98
    SYSCALL("statfs", kPath, kGen, kGen, kGen, kGen, kGen),             // 99
    SYSCALL("fstatfs", kGen, kGen, kGen, kGen, kGen, kGen),             // 100
    SYSCALL("ioperm", kGen, kGen, kGen, kGen, kGen, kGen),              // 101
    SYSCALL("socketcall", kHex, kHex, kHex, kHex, kHex, kHex),          // 102
    SYSCALL("syslog", kGen, kGen, kGen, kGen, kGen, kGen),              // 103
    SYSCALL("setitimer", kGen, kGen, kGen, kGen, kGen, kGen),           // 104
    SYSCALL("getitimer", kGen, kGen, kGen, kGen, kGen, kGen),           // 105
    SYSCALL("stat", kPath, kGen, kGen, kGen, kGen, kGen),               // 106
    SYSCALL("lstat", kPath, kGen, kGen, kGen, kGen, kGen),              // 107
    SYSCALL("fstat", kInt, kHex, kGen, kGen, kGen, kGen),               // 108
    SYSCALL("olduname", kHex, kHex, kHex, kHex, kHex, kHex),            // 109
    SYSCALL("iopl", kGen, kGen, kGen, kGen, kGen, kGen),                // 110
    SYSCALL("vhangup", kGen, kGen, kGen, kGen, kGen, kGen),             // 111
    SYSCALL("idle", kHex, kHex, kHex, kHex, kHex, kHex),                // 112
    SYSCALL("vm86", kHex, kHex, kHex, kHex, kHex, kHex),                // 113
    SYSCALL("wait4", kInt, kHex, kHex, kHex, kGen, kGen),               // 114
    SYSCALL("swapoff", kPath, kGen, kGen, kGen, kGen, kGen),            // 115
    SYSCALL("sysinfo", kGen, kGen, kGen, kGen, kGen, kGen),             // 116
    SYSCALL("ipc", kHex, kHex, kHex, kHex, kHex, kHex),                 // 117
    SYSCALL("fsync", kGen, kGen, kGen, kGen, kGen, kGen),               // 118
    SYSCALL("sigreturn", kHex, kHex, kHex, kHex, kHex, kHex),           // 119
    SYSCALL("clone", kCloneFlag, kHex, kHex, kHex, kHex, kGen),         // 120
    SYSCALL("setdomainname", kGen, kGen, kGen, kGen, kGen, kGen),       // 121
    SYSCALL("uname", kGen, kGen, kGen, kGen, kGen, kGen),               // 122
    SYSCALL("modify_ldt", kGen, kGen, kGen, kGen, kGen, kGen),          // 123
    SYSCALL("adjtimex", kGen, kGen, kGen, kGen, kGen, kGen),            // 124
    SYSCALL("mprotect", kHex, kHex, kHex, kGen, kGen, kGen),            // 125
    SYSCALL("sigprocmask", kHex, kHex, kHex, kHex, kHex, kHex),         // 126
    SYSCALL("create_module", kGen, kGen, kGen, kGen, kGen, kGen),       // 127
    SYSCALL("init_module", kGen, kGen, kGen, kGen, kGen, kGen),         // 128
    SYSCALL("delete_module", kGen, kGen, kGen, kGen, kGen, kGen),       // 129
    SYSCALL("get_kernel_syms", kGen, kGen, kGen, kGen, kGen, kGen),     // 130
    SYSCALL("quotactl", kInt, kPath, kInt, kGen, kGen, kGen),           // 131
    SYSCALL("getpgid", kGen, kGen, kGen, kGen, kGen, kGen),             // 132
    SYSCALL("fchdir", kGen, kGen, kGen, kGen, kGen, kGen),              // 133
    SYSCALL("bdflush", kHex, kHex, kHex, kHex, kHex, kHex),             // 134
    SYSCALL("sysfs", kGen, kGen, kGen, kGen, kGen, kGen),               // 135
    SYSCALL("personality", kGen, kGen, kGen, kGen, kGen, kGen),         // 136
    SYSCALL("afs_syscall", kGen, kGen, kGen, kGen, kGen, kGen),         // 137
    SYSCALL("setfsuid", kGen, kGen, kGen, kGen, kGen, kGen),            // 138
    SYSCALL("setfsgid", kGen, kGen, kGen, kGen, kGen, kGen),            // 139
    SYSCALL("_llseek", kHex, kHex, kHex, kHex, kHex, kHex),             // 140
    SYSCALL("getdents", kGen, kGen, kGen, kGen, kGen, kGen),            // 141
    SYSCALL("_newselect", kHex, kHex, kHex, kHex, kHex, kHex),          // 142
    SYSCALL("flock", kGen, kGen, kGen, kGen, kGen, kGen),               // 143
    SYSCALL("msync", kGen, kGen, kGen, kGen, kGen, kGen),               // 144
    SYSCALL("readv", kGen, kGen, kGen, kGen, kGen, kGen),               // 145
    SYSCALL("writev", kGen, kGen, kGen, kGen, kGen, kGen),              // 146
    SYSCALL("getsid", kGen, kGen, kGen, kGen, kGen, kGen),              // 147
    SYSCALL("fdatasync", kGen, kGen, kGen, kGen, kGen, kGen),           // 148
    SYSCALL("_sysctl", kGen, kGen, kGen, kGen, kGen, kGen),             // 149
    SYSCALL("mlock", kGen, kGen, kGen, kGen, kGen, kGen),               // 150
    SYSCALL("munlock", kGen, kGen, kGen, kGen, kGen, kGen),             // 151
    SYSCALL("mlockall", kGen, kGen, kGen, kGen, kGen, kGen),            // 152
    SYSCALL("munlockall", kGen, kGen, kGen, kGen, kGen, kGen),          // 153
    SYSCALL("sched_setparam", kGen, kGen, kGen, kGen, kGen, kGen),      // 154
    SYSCALL("sched_getparam", kGen, kGen, kGen, kGen, kGen, kGen),      // 155
    SYSCALL("sched_setscheduler", kGen, kGen, kGen, kGen, kGen, kGen),  // 156
    SYSCALL("sched_getscheduler", kGen, kGen, kGen, kGen, kGen, kGen),  // 157
    SYSCALL("sched_yield", kGen, kGen, kGen, kGen, kGen, kGen),         // 158
    SYSCALL("sched_get_priority_max", kGen, kGen, kGen, kGen, kGen,
            kGen),  // 159
    SYSCALL("sched_get_priority_min", kGen, kGen, kGen, kGen, kGen,
            kGen),  // 160
    SYSCALL("sched_rr_get_interval", kGen, kGen, kGen, kGen, kGen,
            kGen),                                                        // 161
    SYSCALL("nanosleep", kHex, kHex, kGen, kGen, kGen, kGen),             // 162
    SYSCALL("mremap", kGen, kGen, kGen, kGen, kGen, kGen),                // 163
    SYSCALL("setresuid", kGen, kGen, kGen, kGen, kGen, kGen),             // 164
    SYSCALL("getresuid", kGen, kGen, kGen, kGen, kGen, kGen),             // 165
    SYSCALL("query_module", kGen, kGen, kGen, kGen, kGen, kGen),          // 166
    SYSCALL("poll", kGen, kGen, kGen, kGen, kGen, kGen),                  // 167
    SYSCALL("nfsservctl", kGen, kGen, kGen, kGen, kGen, kGen),            // 168
    SYSCALL("setresgid", kGen, kGen, kGen, kGen, kGen, kGen),             // 169
    SYSCALL("getresgid", kGen, kGen, kGen, kGen, kGen, kGen),             // 170
    SYSCALL("prctl", kInt, kHex, kHex, kHex, kHex, kGen),                 // 171
    SYSCALL("rt_sigreturn", kGen, kGen, kGen, kGen, kGen, kGen),          // 172
    SYSCALL("rt_sigaction", kSignal, kHex, kHex, kInt, kGen, kGen),       // 173
    SYSCALL("rt_sigprocmask", kGen, kGen, kGen, kGen, kGen, kGen),        // 174
    SYSCALL("rt_sigpending", kGen, kGen, kGen, kGen, kGen, kGen),         // 175
    SYSCALL("rt_sigtimedwait", kGen, kGen, kGen, kGen, kGen, kGen),       // 176
    SYSCALL("rt_sigqueueinfo", kGen, kGen, kGen, kGen, kGen, kGen),       // 177
    SYSCALL("rt_sigsuspend", kGen, kGen, kGen, kGen, kGen, kGen),         // 178
    SYSCALL("pread64", kGen, kGen, kGen, kGen, kGen, kGen),               // 179
    SYSCALL("pwrite64", kGen, kGen, kGen, kGen, kGen, kGen),              // 180
    SYSCALL("chown", kPath, kInt, kInt, kGen, kGen, kGen),                // 181
    SYSCALL("getcwd", kGen, kGen, kGen, kGen, kGen, kGen),                // 182
    SYSCALL("capget", kGen, kGen, kGen, kGen, kGen, kGen),                // 183
    SYSCALL("capset", kGen, kGen, kGen, kGen, kGen, kGen),                // 184
    SYSCALL("sigaltstack", kGen, kGen, kGen, kGen, kGen, kGen),           // 185
    SYSCALL("sendfile", kGen, kGen, kGen, kGen, kGen, kGen),              // 186
    SYSCALL("getpmsg", kGen, kGen, kGen, kGen, kGen, kGen),               // 187
    SYSCALL("putpmsg", kGen, kGen, kGen, kGen, kGen, kGen),               // 188
    SYSCALL("vfork", kGen, kGen, kGen, kGen, kGen, kGen),                 // 189
    SYSCALL("ugetrlimit", kHex, kHex, kHex, kHex, kHex, kHex),            // 190
    SYSCALL("readahead", kGen, kGen, kGen, kGen, kGen, kGen),             // 191
    SYSCALL("mmap2", kHex, kHex, kHex, kHex, kHex, kHex),                 // 192
    SYSCALL("truncate64", kHex, kHex, kHex, kHex, kHex, kHex),            // 193
    SYSCALL("ftruncate64", kHex, kHex, kHex, kHex, kHex, kHex),           // 194
    SYSCALL("stat64", kHex, kHex, kHex, kHex, kHex, kHex),                // 195
    SYSCALL("lstat64", kHex, kHex, kHex, kHex, kHex, kHex),               // 196
    SYSCALL("fstat64", kHex, kHex, kHex, kHex, kHex, kHex),               // 197
    SYSCALL("pciconfig_read", kHex, kHex, kHex, kHex, kHex, kHex),        // 198
    SYSCALL("pciconfig_write", kHex, kHex, kHex, kHex, kHex, kHex),       // 199
    SYSCALL("pciconfig_iobase", kHex, kHex, kHex, kHex, kHex, kHex),      // 200
    SYSCALL("multiplexer", kHex, kHex, kHex, kHex, kHex, kHex),           // 201
    SYSCALL("getdents64", kGen, kGen, kGen, kGen, kGen, kGen),            // 202
    SYSCALL("pivot_root", kPath, kPath, kGen, kGen, kGen, kGen),          // 203
    SYSCALL("fcntl64", kHex, kHex, kHex, kHex, kHex, kHex),               // 204
    SYSCALL("madvise", kGen, kGen, kGen, kGen, kGen, kGen),               // 205
    SYSCALL("mincore", kGen, kGen, kGen, kGen, kGen, kGen),               // 206
    SYSCALL("gettid", kGen, kGen, kGen, kGen, kGen, kGen),                // 207
    SYSCALL("tkill", kInt, kSignal, kGen, kGen, kGen, kGen),              // 208
    SYSCALL("setxattr", kPath, kString, kGen, kInt, kHex, kGen),          // 209
    SYSCALL("lsetxattr", kPath, kString, kGen, kInt, kHex, kGen),         // 210
    SYSCALL("fsetxattr", kGen, kGen, kGen, kGen, kGen, kGen),             // 211
    SYSCALL("getxattr", kPath, kString, kGen, kInt, kGen, kGen),          // 212
    SYSCALL("lgetxattr", kPath, kString, kGen, kInt, kGen, kGen),         // 213
    SYSCALL("fgetxattr", kGen, kGen, kGen, kGen, kGen, kGen),             // 214
    SYSCALL("listxattr", kPath, kGen, kInt, kGen, kGen, kGen),            // 215
    SYSCALL("llistxattr", kPath, kGen, kInt, kGen, kGen, kGen),           // 216
    SYSCALL("flistxattr", kGen, kGen, kGen, kGen, kGen, kGen),            // 217
    SYSCALL("removexattr", kPath, kString, kGen, kGen, kGen, kGen),       // 218
    SYSCALL("lremovexattr", kGen, kGen, kGen, kGen, kGen, kGen),          // 219
    SYSCALL("fremovexattr", kGen, kGen, kGen, kGen, kGen, kGen),          // 220
    SYSCALL("futex", kGen, kGen, kGen, kGen, kGen, kGen),                 // 221
    SYSCALL("sched_setaffinity", kGen, kGen, kGen, kGen, kGen, kGen),     // 222
    SYSCALL("sched_getaffinity", kGen, kGen, kGen, kGen, kGen, kGen),     // 223
    SYSCALLS_UNUSED("UNUSED224"),                                         // 224
    SYSCALL("tuxcall", kGen, kGen, kGen, kGen, kGen, kGen),               // 225
    SYSCALL("sendfile64", kHex, kHex, kHex, kHex, kHex, kHex),            // 226
    SYSCALL("io_setup", kGen, kGen, kGen, kGen, kGen, kGen),              // 227
    SYSCALL("io_destroy", kGen, kGen, kGen, kGen, kGen, kGen),            // 228
    SYSCALL("io_getevents", kGen, kGen, kGen, kGen, kGen, kGen),          // 229
    SYSCALL("io_submit", kGen, kGen, kGen, kGen, kGen, kGen),             // 230
    SYSCALL("io_cancel", kGen, kGen, kGen, kGen, kGen, kGen),             // 231
    SYSCALL("set_tid_address", kHex, kGen, kGen, kGen, kGen, kGen),       // 232
    SYSCALL("fadvise64", kGen, kGen, kGen, kGen, kGen, kGen),             // 233
    SYSCALL("exit_group", kInt, kGen, kGen, kGen, kGen, kGen),            // 234
    SYSCALL("lookup_dcookie", kGen, kGen, kGen, kGen, kGen, kGen),        // 235
    SYSCALL("epoll_create", kGen, kGen, kGen, kGen, kGen, kGen),          // 236
    SYSCALL("epoll_ctl", kGen, kGen, kGen, kGen, kGen, kGen),             // 237
    SYSCALL("epoll_wait", kGen, kGen, kGen, kGen, kGen, kGen),            // 238
    SYSCALL("remap_file_pages", kGen, kGen, kGen, kGen, kGen, kGen),      // 239
    SYSCALL("timer_create", kGen, kGen, kGen, kGen, kGen, kGen),          // 240
    SYSCALL("timer_settime", kGen, kGen, kGen, kGen, kGen, kGen),         // 241
    SYSCALL("timer_gettime", kGen, kGen, kGen, kGen, kGen, kGen),         // 242
    SYSCALL("timer_getoverrun", kGen, kGen, kGen, kGen, kGen, kGen),      // 243
    SYSCALL("timer_delete", kGen, kGen, kGen, kGen, kGen, kGen),          // 244
    SYSCALL("clock_settime", kGen, kGen, kGen, kGen, kGen, kGen),         // 245
    SYSCALL("clock_gettime", kGen, kGen, kGen, kGen, kGen, kGen),         // 246
    SYSCALL("clock_getres", kGen, kGen, kGen, kGen, kGen, kGen),          // 247
    SYSCALL("clock_nanosleep", kGen, kGen, kGen, kGen, kGen, kGen),       // 248
    SYSCALL("swapcontext", kHex, kHex, kHex, kHex, kHex, kHex),           // 249
    SYSCALL("tgkill", kInt, kInt, kSignal, kGen, kGen, kGen),             // 250
    SYSCALL("utimes", kGen, kGen, kGen, kGen, kGen, kGen),                // 251
    SYSCALL("statfs64", kHex, kHex, kHex, kHex, kHex, kHex),              // 252
    SYSCALL("fstatfs64", kHex, kHex, kHex, kHex, kHex, kHex),             // 253
    SYSCALL("fadvise64_64", kHex, kHex, kHex, kHex, kHex, kHex),          // 254
    SYSCALL("rtas", kHex, kHex, kHex, kHex, kHex, kHex),                  // 255
    SYSCALL("sys_debug_setcontext", kHex, kHex, kHex, kHex, kHex, kHex),  // 256
    SYSCALLS_UNUSED("UNUSED257"),                                         // 257
    SYSCALL("migrate_pages", kGen, kGen, kGen, kGen, kGen, kGen),         // 258
    SYSCALL("mbind", kGen, kGen, kGen, kGen, kGen, kGen),                 // 259
    SYSCALL("get_mempolicy", kGen, kGen, kGen, kGen, kGen, kGen),         // 260
    SYSCALL("set_mempolicy", kGen, kGen, kGen, kGen, kGen, kGen),         // 261
    SYSCALL("mq_open", kGen, kGen, kGen, kGen, kGen, kGen),               // 262
    SYSCALL("mq_unlink", kGen, kGen, kGen, kGen, kGen, kGen),             // 263
    SYSCALL("mq_timedsend", kGen, kGen, kGen, kGen, kGen, kGen),          // 264
    SYSCALL("mq_timedreceive", kGen, kGen, kGen, kGen, kGen, kGen),       // 265
    SYSCALL("mq_notify", kGen, kGen, kGen, kGen, kGen, kGen),             // 266
    SYSCALL("mq_getsetattr", kGen, kGen, kGen, kGen, kGen, kGen),         // 267
    SYSCALL("kexec_load", kGen, kGen, kGen, kGen, kGen, kGen),            // 268
    SYSCALL("add_key", kGen, kGen, kGen, kGen, kGen, kGen),               // 269
    SYSCALL("request_key", kGen, kGen, kGen, kGen, kGen, kGen),           // 270
    SYSCALL("keyctl", kGen, kGen, kGen, kGen, kGen, kGen),                // 271
    SYSCALL("waitid", kGen, kGen, kGen, kGen, kGen, kGen),                // 272
    SYSCALL("ioprio_set", kGen, kGen, kGen, kGen, kGen, kGen),            // 273
    SYSCALL("ioprio_get", kGen, kGen, kGen, kGen, kGen, kGen),            // 274
    SYSCALL("inotify_init", kGen, kGen, kGen, kGen, kGen, kGen),          // 275
    SYSCALL("inotify_add_watch", kGen, kGen, kGen, kGen, kGen, kGen),     // 276
    SYSCALL("inotify_rm_watch", kGen, kGen, kGen, kGen, kGen, kGen),      // 277
    SYSCALL("spu_run", kHex, kHex, kHex, kHex, kHex, kHex),               // 278
    SYSCALL("spu_create", kHex, kHex, kHex, kHex, kHex, kHex),            // 279
    SYSCALL("pselect6", kGen, kGen, kGen, kGen, kGen, kGen),              // 280
    SYSCALL("ppoll", kGen, kGen, kGen, kGen, kGen, kGen),                 // 281
    SYSCALL("unshare", kGen, kGen, kGen, kGen, kGen, kGen),               // 282
    SYSCALL("splice", kGen, kGen, kGen, kGen, kGen, kGen),                // 283
    SYSCALL("tee", kGen, kGen, kGen, kGen, kGen, kGen),                   // 284
    SYSCALL("vmsplice", kGen, kGen, kGen, kGen, kGen, kGen),              // 285
    SYSCALL("openat", kGen, kPath, kOct, kHex, kGen, kGen),               // 286
    SYSCALL("mkdirat", kGen, kPath, kGen, kGen, kGen, kGen),              // 287
    SYSCALL("mknodat", kGen, kPath, kGen, kGen, kGen, kGen),              // 288
    SYSCALL("fchownat", kGen, kPath, kGen, kGen, kGen, kGen),             // 289
    SYSCALL("futimesat", kGen, kPath, kGen, kGen, kGen, kGen),            // 290
    SYSCALL("newfstatat", kGen, kPath, kGen, kGen, kGen, kGen),           // 291
    SYSCALL("unlinkat", kGen, kPath, kGen, kGen, kGen, kGen),             // 292
    SYSCALL("renameat", kGen, kPath, kGen, kPath, kGen, kGen),            // 293
    SYSCALL("linkat", kGen, kPath, kGen, kPath, kGen, kGen),              // 294
    SYSCALL("symlinkat", kPath, kGen, kPath, kGen, kGen, kGen),           // 295
    SYSCALL("readlinkat", kGen, kPath, kGen, kGen, kGen, kGen),           // 296
    SYSCALL("fchmodat", kGen, kPath, kGen, kGen, kGen, kGen),             // 297
    SYSCALL("faccessat", kGen, kPath, kGen, kGen, kGen, kGen),            // 298
    SYSCALL("get_robust_list", kGen, kGen, kGen, kGen, kGen, kGen),       // 299
    SYSCALL("set_robust_list", kGen, kGen, kGen, kGen, kGen, kGen),       // 300
    SYSCALL("move_pages", kGen, kGen, kGen, kGen, kGen, kGen),            // 301
    SYSCALL("getcpu", kHex, kHex, kHex, kGen, kGen, kGen),                // 302
    SYSCALL("epoll_pwait", kGen, kGen, kGen, kGen, kGen, kGen),           // 303
    SYSCALL("utimensat", kGen, kGen, kGen, kGen, kGen, kGen),             // 304
    SYSCALL("signalfd", kGen, kGen, kGen, kGen, kGen, kGen),              // 305
    SYSCALL("timerfd_create", kGen, kGen, kGen, kGen, kGen, kGen),        // 306
    SYSCALL("eventfd", kGen, kGen, kGen, kGen, kGen, kGen),               // 307
    SYSCALL("sync_file_range2", kHex, kHex, kHex, kHex, kHex, kHex),      // 308
    SYSCALL("fallocate", kGen, kGen, kGen, kGen, kGen, kGen),             // 309
    SYSCALL("subpage_prot", kHex, kHex, kHex, kHex, kHex, kHex),          // 310
    SYSCALL("timerfd_settime", kGen, kGen, kGen, kGen, kGen, kGen),       // 311
    SYSCALL("timerfd_gettime", kGen, kGen, kGen, kGen, kGen, kGen),       // 312
    SYSCALL("signalfd4", kGen, kGen, kGen, kGen, kGen, kGen),             // 313
    SYSCALL("eventfd2", kGen, kGen, kGen, kGen, kGen, kGen),              // 314
    SYSCALL("epoll_create1", kGen, kGen, kGen, kGen, kGen, kGen),         // 315
    SYSCALL("dup3", kGen, kGen, kGen, kGen, kGen, kGen),                  // 316
    SYSCALL("pipe2", kGen, kGen, kGen, kGen, kGen, kGen),                 // 317
    SYSCALL("inotify_init1", kGen, kGen, kGen, kGen, kGen, kGen),         // 318
    SYSCALL("perf_event_open", kGen, kGen, kGen, kGen, kGen, kGen),       // 319
    SYSCALL("preadv", kGen, kGen, kGen, kGen, kGen, kGen),                // 320
    SYSCALL("pwritev", kGen, kGen, kGen, kGen, kGen, kGen),               // 321
    SYSCALL("rt_tgsigqueueinfo", kGen, kGen, kGen, kGen, kGen, kGen),     // 322
    SYSCALL("fanotify_init", kHex, kHex, kInt, kGen, kGen, kGen),         // 323
    SYSCALL("fanotify_mark", kInt, kHex, kInt, kPath, kGen, kGen),        // 324
    SYSCALL("prlimit64", kInt, kInt, kHex, kHex, kGen, kGen),             // 325
    SYSCALL("socket", kAddressFamily, kInt, kInt, kGen, kGen, kGen),      // 326
    SYSCALL("bind", kGen, kGen, kGen, kGen, kGen, kGen),                  // 327
    SYSCALL("connect", kInt, kSockaddr, kInt, kGen, kGen, kGen),          // 328
    SYSCALL("listen", kGen, kGen, kGen, kGen, kGen, kGen),                // 329
    SYSCALL("accept", kGen, kGen, kGen, kGen, kGen, kGen),                // 330
    SYSCALL("getsockname", kGen, kGen, kGen, kGen, kGen, kGen),           // 331
    SYSCALL("getpeername", kGen, kGen, kGen, kGen, kGen, kGen),           // 332
    SYSCALL("socketpair", kGen, kGen, kGen, kGen, kGen, kGen),            // 333
    SYSCALL("send", kHex, kHex, kHex, kHex, kHex, kHex),                  // 334
    SYSCALL("sendto", kInt, kGen, kInt, kHex, kSockaddr, kInt),           // 335
    SYSCALL("recv", kHex, kHex, kHex, kHex, kHex, kHex),                  // 336
    SYSCALL("recvfrom", kGen, kGen, kGen, kGen, kGen, kGen),              // 337
    SYSCALL("shutdown", kGen, kGen, kGen, kGen, kGen, kGen),              // 338
    SYSCALL("setsockopt", kGen, kGen, kGen, kGen, kGen, kGen),            // 339
    SYSCALL("getsockopt", kGen, kGen, kGen, kGen, kGen, kGen),            // 340
    SYSCALL("sendmsg", kInt, kSockmsghdr, kHex, kGen, kGen, kGen),        // 341
    SYSCALL("recvmsg", kGen, kGen, kGen, kGen, kGen, kGen),               // 342
    SYSCALL("recvmmsg", kInt, kHex, kHex, kHex, kGen, kGen),              // 343
    SYSCALL("accept4", kGen, kGen, kGen, kGen, kGen, kGen),               // 344
    SYSCALL("name_to_handle_at", kInt, kGen, kHex, kHex, kHex, kGen),     // 345
    SYSCALL("open_by_handle_at", kInt, kHex, kHex, kGen, kGen, kGen),     // 346
    SYSCALL("clock_adjtime", kInt, kHex, kGen, kGen, kGen, kGen),         // 347
    SYSCALL("syncfs", kInt, kGen, kGen, kGen, kGen, kGen),                // 348
    SYSCALL("sendmmsg", kInt, kHex, kInt, kHex, kGen, kGen),              // 349
    SYSCALL("setns", kInt, kHex, kGen, kGen, kGen, kGen),                 // 350
    SYSCALL("process_vm_readv", kInt, kHex, kInt, kHex, kInt, kInt),      // 351
    SYSCALL("process_vm_writev", kInt, kHex, kInt, kHex, kInt, kInt),     // 352
    SYSCALL("finit_module", kInt, kPath, kHex, kGen, kGen, kGen),         // 353
    SYSCALL("kcmp", kInt, kInt, kInt, kHex, kHex, kGen),                  // 354
    SYSCALL("sched_setattr", kGen, kGen, kGen, kGen, kGen, kGen),         // 355
    SYSCALL("sched_getattr", kGen, kGen, kGen, kGen, kGen, kGen),         // 356
    SYSCALL("renameat2", kGen, kPath, kGen, kPath, kGen, kGen),           // 357
    SYSCALL("seccomp", kGen, kGen, kGen, kGen, kGen, kGen),               // 358
    SYSCALL("getrandom", kGen, kGen, kGen, kGen, kGen, kGen),             // 359
    SYSCALL("memfd_create", kGen, kGen, kGen, kGen, kGen, kGen),          // 360
    SYSCALL("bpf", kHex, kHex, kHex, kHex, kHex, kHex),                   // 361
    SYSCALL("execveat", kHex, kHex, kHex, kHex, kHex, kHex),              // 362
    SYSCALL("switch_endian", kHex, kHex, kHex, kHex, kHex, kHex),         // 363
    SYSCALL("userfaultfd", kHex, kHex, kHex, kHex, kHex, kHex),           // 364
    SYSCALL("membarrier", kHex, kHex, kHex, kHex, kHex, kHex),            // 365
    SYSCALLS_UNUSED("UNUSED366"),                                         // 366
    SYSCALLS_UNUSED("UNUSED367"),                                         // 367
    SYSCALLS_UNUSED("UNUSED368"),                                         // 368
    SYSCALLS_UNUSED("UNUSED369"),                                         // 369
    SYSCALLS_UNUSED("UNUSED370"),                                         // 370
    SYSCALLS_UNUSED("UNUSED371"),                                         // 371
    SYSCALLS_UNUSED("UNUSED372"),                                         // 372
    SYSCALLS_UNUSED("UNUSED373"),                                         // 373
    SYSCALLS_UNUSED("UNUSED374"),                                         // 374
    SYSCALLS_UNUSED("UNUSED375"),                                         // 375
    SYSCALLS_UNUSED("UNUSED376"),                                         // 376
    SYSCALLS_UNUSED("UNUSED377"),                                         // 377
    SYSCALL("mlock2", kHex, kHex, kHex, kHex, kHex, kHex),                // 378
    SYSCALL("copy_file_range", kHex, kHex, kHex, kHex, kHex, kHex),       // 379
    SYSCALL("preadv2", kHex, kHex, kHex, kHex, kHex, kHex),               // 380
    SYSCALL("pwritev2", kHex, kHex, kHex, kHex, kHex, kHex),              // 381
};

#endif

#undef SYSCALLS_UNUSED00_99
#undef SYSCALLS_UNUSED50_99
#undef SYSCALLS_UNUSED00_49
#undef SYSCALLS_UNUSED0_9
#undef SYSCALLS_UNUSED

#undef SYSCALL_HELPER
#undef SYSCALL_NUM_ARGS_HELPER
#undef SYSCALL_NUM_ARGS
#undef SYSCALL
#undef SYSCALL_WITH_UNKNOWN_ARGS

}  // namespace sandbox2
