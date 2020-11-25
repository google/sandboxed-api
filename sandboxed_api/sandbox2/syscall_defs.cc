#include "sandboxed_api/sandbox2/syscall_defs.h"

#include <cstdint>
#include <type_traits>

#include <glog/logging.h>
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "sandboxed_api/sandbox2/config.h"
#include "sandboxed_api/sandbox2/util.h"

namespace sandbox2 {

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
struct SyscallTable::Entry {
  // Returns the number of arguments which given syscall takes.
  int GetNumArgs() const {
    if (num_args < 0 || num_args > syscalls::kMaxArgs) {
      return syscalls::kMaxArgs;
    }
    return num_args;
  }

  static std::string GetArgumentDescription(uint64_t value, ArgType type,
                                            pid_t pid);

  std::vector<std::string> GetArgumentsDescription(
      const uint64_t values[syscalls::kMaxArgs], pid_t pid) const;

  const absl::string_view name;
  const int num_args;
  const std::array<ArgType, syscalls::kMaxArgs> arg_types;
};

std::string SyscallTable::Entry::GetArgumentDescription(uint64_t value,
                                                        ArgType type,
                                                        pid_t pid) {
  std::string ret = absl::StrFormat("%#x", value);
  switch (type) {
    case kOct:
      absl::StrAppendFormat(&ret, " [\\0%o]", value);
      break;
    case kPath:
      if (auto path_or = util::ReadCPathFromPid(pid, value); path_or.ok()) {
        absl::StrAppendFormat(&ret, " ['%s']",
                              absl::CHexEscape(path_or.value()));
      } else {
        absl::StrAppend(&ret, " [unreadable path]");
      }
      break;
    case kInt:
      absl::StrAppendFormat(&ret, " [%d]", value);
      break;
    default:
      break;
  }
  return ret;
}

template <typename... ArgTypes>
constexpr SyscallTable::Entry MakeEntry(absl::string_view name,
                                        ArgTypes... arg_types) {
  static_assert(sizeof...(arg_types) <= syscalls::kMaxArgs,
                "Too many arguments for syscall");
  return {name, sizeof...(arg_types), {arg_types...}};
}

struct UnknownArguments {};
constexpr SyscallTable::Entry MakeEntry(absl::string_view name,
                                        UnknownArguments) {
  return {name, -1, {kGen, kGen, kGen, kGen, kGen, kGen}};
}

absl::string_view SyscallTable::GetName(int syscall) const {
  return syscall < data_.size() ? data_[syscall].name : "";
}

std::vector<std::string> SyscallTable::GetArgumentsDescription(
    int syscall, const uint64_t values[], pid_t pid) const {
  static auto kInvalidEntry = MakeEntry("", kGen, kGen, kGen, kGen, kGen, kGen);
  const auto& entry = syscall < data_.size() ? data_[syscall] : kInvalidEntry;

  int num_args = entry.GetNumArgs();
  std::vector<std::string> rv;
  rv.reserve(num_args);
  for (int i = 0; i < num_args; ++i) {
    rv.push_back(SyscallTable::Entry::GetArgumentDescription(
        values[i], entry.arg_types[i], pid));
  }
  return rv;
}

#define SYSCALLS_UNUSED(name) \
  MakeEntry(name, kHex, kHex, kHex, kHex, kHex, kHex)

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

// Syscall description table for Linux x86_64
constexpr std::array kSyscallDataX8664 = {
    MakeEntry("read", kInt, kHex, kInt),                             // 0
    MakeEntry("write", kInt, kHex, kInt),                            // 1
    MakeEntry("open", kPath, kHex, kOct),                            // 2
    MakeEntry("close", kInt),                                        // 3
    MakeEntry("stat", kPath, kGen),                                  // 4
    MakeEntry("fstat", kInt, kHex),                                  // 5
    MakeEntry("lstat", kPath, kGen),                                 // 6
    MakeEntry("poll", kGen, kInt, kInt),                             // 7
    MakeEntry("lseek", kInt, kInt, kInt),                            // 8
    MakeEntry("mmap", kHex, kInt, kHex, kHex, kInt, kInt),           // 9
    MakeEntry("mprotect", kHex, kInt, kHex),                         // 10
    MakeEntry("munmap", kHex, kInt),                                 // 11
    MakeEntry("brk", kInt),                                          // 12
    MakeEntry("rt_sigaction", kSignal, kHex, kHex, kInt),            // 13
    MakeEntry("rt_sigprocmask", kInt, kHex, kHex, kInt),             // 14
    MakeEntry("rt_sigreturn"),                                       // 15
    MakeEntry("ioctl", kInt, kInt, kHex),                            // 16
    MakeEntry("pread64", kInt, kHex, kInt, kInt),                    // 17
    MakeEntry("pwrite64", kInt, kHex, kInt, kInt),                   // 18
    MakeEntry("readv", kInt, kHex, kInt),                            // 19
    MakeEntry("writev", kInt, kHex, kInt),                           // 20
    MakeEntry("access", kPath, kOct),                                // 21
    MakeEntry("pipe", kHex),                                         // 22
    MakeEntry("select", kInt, kHex, kHex, kHex, kHex),               // 23
    MakeEntry("sched_yield"),                                        // 24
    MakeEntry("mremap", kHex, kInt, kInt, kInt, kHex),               // 25
    MakeEntry("msync", kHex, kInt, kInt),                            // 26
    MakeEntry("mincore", kHex, kInt, kHex),                          // 27
    MakeEntry("madvise", kHex, kInt, kInt),                          // 28
    MakeEntry("shmget", kInt, kInt, kHex),                           // 29
    MakeEntry("shmat", kInt, kHex, kHex),                            // 30
    MakeEntry("shmctl", kInt, kInt, kHex),                           // 31
    MakeEntry("dup", kInt),                                          // 32
    MakeEntry("dup2", kInt, kInt),                                   // 33
    MakeEntry("pause"),                                              // 34
    MakeEntry("nanosleep", kHex, kHex),                              // 35
    MakeEntry("getitimer", kInt, kHex),                              // 36
    MakeEntry("alarm", kInt),                                        // 37
    MakeEntry("setitimer", kInt, kHex, kHex),                        // 38
    MakeEntry("getpid"),                                             // 39
    MakeEntry("sendfile", kInt, kInt, kHex, kInt),                   // 40
    MakeEntry("socket", kAddressFamily, kInt, kInt),                 // 41
    MakeEntry("connect", kInt, kSockaddr, kInt),                     // 42
    MakeEntry("accept", kInt, kSockaddr, kHex),                      // 43
    MakeEntry("sendto", kInt, kHex, kInt, kHex, kSockaddr, kInt),    // 44
    MakeEntry("recvfrom", kInt, kHex, kInt, kHex, kSockaddr, kHex),  // 45
    MakeEntry("sendmsg", kInt, kSockmsghdr, kHex),                   // 46
    MakeEntry("recvmsg", kInt, kHex, kInt),                          // 47
    MakeEntry("shutdown", kInt, kInt),                               // 48
    MakeEntry("bind", kInt, kSockaddr, kInt),                        // 49
    MakeEntry("listen", kInt, kInt),                                 // 50
    MakeEntry("getsockname", kInt, kSockaddr, kHex),                 // 51
    MakeEntry("getpeername", kInt, kSockaddr, kHex),                 // 52
    MakeEntry("socketpair", kAddressFamily, kInt, kInt, kHex),       // 53
    MakeEntry("setsockopt", kInt, kInt, kInt, kHex, kHex),           // 54
    MakeEntry("getsockopt", kInt, kInt, kInt, kHex, kInt),           // 55
    MakeEntry("clone", kCloneFlag, kHex, kHex, kHex, kHex),          // 56
    MakeEntry("fork"),                                               // 57
    MakeEntry("vfork"),                                              // 58
    MakeEntry("execve", kPath, kHex, kHex),                          // 59
    MakeEntry("exit", kInt),                                         // 60
    MakeEntry("wait4", kInt, kHex, kHex, kHex),                      // 61
    MakeEntry("kill", kInt, kSignal),                                // 62
    MakeEntry("uname", kInt),                                        // 63
    MakeEntry("semget", kInt, kInt, kHex),                           // 64
    MakeEntry("semop", kInt, kHex, kInt),                            // 65
    MakeEntry("semctl", kInt, kInt, kInt, kHex),                     // 66
    MakeEntry("shmdt", kHex),                                        // 67
    MakeEntry("msgget", kInt, kHex),                                 // 68
    MakeEntry("msgsnd", kInt, kHex, kInt, kHex),                     // 69
    MakeEntry("msgrcv", kInt, kHex, kInt, kInt, kHex),               // 70
    MakeEntry("msgctl", kInt, kInt, kHex),                           // 71
    MakeEntry("fcntl", kInt, kInt, kHex),                            // 72
    MakeEntry("flock", kInt, kInt),                                  // 73
    MakeEntry("fsync", kInt),                                        // 74
    MakeEntry("fdatasync", kInt),                                    // 75
    MakeEntry("truncate", kPath, kInt),                              // 76
    MakeEntry("ftruncate", kInt, kInt),                              // 77
    MakeEntry("getdents", kInt, kHex, kInt),                         // 78
    MakeEntry("getcwd", kHex, kInt),                                 // 79
    MakeEntry("chdir", kPath),                                       // 80
    MakeEntry("fchdir", kInt),                                       // 81
    MakeEntry("rename", kPath, kPath),                               // 82
    MakeEntry("mkdir", kPath, kOct),                                 // 83
    MakeEntry("rmdir", kPath),                                       // 84
    MakeEntry("creat", kPath, kOct),                                 // 85
    MakeEntry("link", kPath, kPath),                                 // 86
    MakeEntry("unlink", kPath),                                      // 87
    MakeEntry("symlink", kPath, kPath),                              // 88
    MakeEntry("readlink", kPath, kHex, kInt),                        // 89
    MakeEntry("chmod", kPath, kOct),                                 // 90
    MakeEntry("fchmod", kInt, kOct),                                 // 91
    MakeEntry("chown", kPath, kInt, kInt),                           // 92
    MakeEntry("fchown", kInt, kInt, kInt),                           // 93
    MakeEntry("lchown", kPath, kInt, kInt),                          // 94
    MakeEntry("umask", kHex),                                        // 95
    MakeEntry("gettimeofday", kHex, kHex),                           // 96
    MakeEntry("getrlimit", kInt, kHex),                              // 97
    MakeEntry("getrusage", kInt, kHex),                              // 98
    MakeEntry("sysinfo", kHex),                                      // 99
    MakeEntry("times", kHex),                                        // 100
    MakeEntry("ptrace", kInt, kInt, kHex, kHex),                     // 101
    MakeEntry("getuid"),                                             // 102
    MakeEntry("syslog", kInt, kHex, kInt),                           // 103
    MakeEntry("getgid"),                                             // 104
    MakeEntry("setuid", kInt),                                       // 105
    MakeEntry("setgid", kInt),                                       // 106
    MakeEntry("geteuid"),                                            // 107
    MakeEntry("getegid"),                                            // 108
    MakeEntry("setpgid", kInt, kInt),                                // 109
    MakeEntry("getppid"),                                            // 110
    MakeEntry("getpgrp"),                                            // 111
    MakeEntry("setsid"),                                             // 112
    MakeEntry("setreuid", kInt, kInt),                               // 113
    MakeEntry("setregid", kInt, kInt),                               // 114
    MakeEntry("getgroups", kInt, kHex),                              // 115
    MakeEntry("setgroups", kInt, kHex),                              // 116
    MakeEntry("setresuid", kInt, kInt, kInt),                        // 117
    MakeEntry("getresuid", kHex, kHex, kHex),                        // 118
    MakeEntry("setresgid", kInt, kInt, kInt),                        // 119
    MakeEntry("getresgid", kHex, kHex, kHex),                        // 120
    MakeEntry("getpgid", kInt),                                      // 121
    MakeEntry("setfsuid", kInt),                                     // 122
    MakeEntry("setfsgid", kInt),                                     // 123
    MakeEntry("getsid", kInt),                                       // 124
    MakeEntry("capget", kHex, kHex),                                 // 125
    MakeEntry("capset", kHex, kHex),                                 // 126
    MakeEntry("rt_sigpending", kHex, kInt),                          // 127
    MakeEntry("rt_sigtimedwait", kHex, kHex, kHex, kInt),            // 128
    MakeEntry("rt_sigqueueinfo", kInt, kSignal, kHex),               // 129
    MakeEntry("rt_sigsuspend", kHex, kInt),                          // 130
    MakeEntry("sigaltstack", kHex, kHex),                            // 131
    MakeEntry("utime", kPath, kHex),                                 // 132
    MakeEntry("mknod", kPath, kOct, kHex),                           // 133
    MakeEntry("uselib", kPath),                                      // 134
    MakeEntry("personality", kHex),                                  // 135
    MakeEntry("ustat", kHex, kHex),                                  // 136
    MakeEntry("statfs", kPath, kHex),                                // 137
    MakeEntry("fstatfs", kInt, kHex),                                // 138
    MakeEntry("sysfs", kInt, kInt, kInt),                            // 139
    MakeEntry("getpriority", kInt, kInt),                            // 140
    MakeEntry("setpriority", kInt, kInt, kInt),                      // 141
    MakeEntry("sched_setparam", kInt, kHex),                         // 142
    MakeEntry("sched_getparam", kInt, kHex),                         // 143
    MakeEntry("sched_setscheduler", kInt, kInt, kHex),               // 144
    MakeEntry("sched_getscheduler", kInt),                           // 145
    MakeEntry("sched_get_priority_max", kInt),                       // 146
    MakeEntry("sched_get_priority_min", kInt),                       // 147
    MakeEntry("sched_rr_get_interval", kInt, kHex),                  // 148
    MakeEntry("mlock", kInt, kInt),                                  // 149
    MakeEntry("munlock", kInt, kInt),                                // 150
    MakeEntry("mlockall", kHex),                                     // 151
    MakeEntry("munlockall"),                                         // 152
    MakeEntry("vhangup"),                                            // 153
    MakeEntry("modify_ldt", kInt, kHex, kInt),                       // 154
    MakeEntry("pivot_root", kPath, kPath),                           // 155
    MakeEntry("_sysctl", kHex),                                      // 156
    MakeEntry("prctl", kInt, kHex, kHex, kHex, kHex),                // 157
    MakeEntry("arch_prctl", kInt, kHex),                             // 158
    MakeEntry("adjtimex", kHex),                                     // 159
    MakeEntry("setrlimit", kInt, kHex),                              // 160
    MakeEntry("chroot", kPath),                                      // 161
    MakeEntry("sync"),                                               // 162
    MakeEntry("acct", kPath),                                        // 163
    MakeEntry("settimeofday", kHex, kHex),                           // 164
    MakeEntry("mount", kPath, kPath, kString, kHex, kGen),           // 165
    MakeEntry("umount2", kPath, kHex),                               // 166
    MakeEntry("swapon", kPath, kHex),                                // 167
    MakeEntry("swapoff", kPath),                                     // 168
    MakeEntry("reboot", kInt, kHex, kHex, kGen),                     // 169
    MakeEntry("sethostname", kString, kInt),                         // 170
    MakeEntry("setdomainname", kString, kInt),                       // 171
    MakeEntry("iopl", kInt),                                         // 172
    MakeEntry("ioperm", kInt, kInt, kInt),                           // 173
    MakeEntry("create_module", kString, kInt),                       // 174
    MakeEntry("init_module", kGen, kInt, kString),                   // 175
    MakeEntry("delete_module", kString, kHex),                       // 176
    MakeEntry("get_kernel_syms", kHex),                              // 177
    MakeEntry("query_module", kString, kInt, kGen, kInt, kGen),      // 178
    MakeEntry("quotactl", kInt, kPath, kInt, kGen),                  // 179
    MakeEntry("nfsservctl", kInt, kGen, kGen),                       // 180
    MakeEntry("getpmsg", UnknownArguments()),                        // 181
    MakeEntry("putpmsg", UnknownArguments()),                        // 182
    MakeEntry("afs_syscall", UnknownArguments()),                    // 183
    MakeEntry("tuxcall", UnknownArguments()),                        // 184
    MakeEntry("security", UnknownArguments()),                       // 185
    MakeEntry("gettid"),                                             // 186
    MakeEntry("readahead", kInt, kInt, kInt),                        // 187
    MakeEntry("setxattr", kPath, kString, kGen, kInt, kHex),         // 188
    MakeEntry("lsetxattr", kPath, kString, kGen, kInt, kHex),        // 189
    MakeEntry("fsetxattr", kInt, kString, kGen, kInt, kHex),         // 190
    MakeEntry("getxattr", kPath, kString, kGen, kInt),               // 191
    MakeEntry("lgetxattr", kPath, kString, kGen, kInt),              // 192
    MakeEntry("fgetxattr", kInt, kString, kGen, kInt),               // 193
    MakeEntry("listxattr", kPath, kGen, kInt),                       // 194
    MakeEntry("llistxattr", kPath, kGen, kInt),                      // 195
    MakeEntry("flistxattr", kInt, kGen, kInt),                       // 196
    MakeEntry("removexattr", kPath, kString),                        // 197
    MakeEntry("lremovexattr", kPath, kString),                       // 198
    MakeEntry("fremovexattr", kInt, kString),                        // 199
    MakeEntry("tkill", kInt, kSignal),                               // 200
    MakeEntry("time", kHex),                                         // 201
    MakeEntry("futex", kGen, kInt, kInt, kGen, kGen, kInt),          // 202
    MakeEntry("sched_setaffinity", kInt, kInt, kHex),                // 203
    MakeEntry("sched_getaffinity", kInt, kInt, kHex),                // 204
    MakeEntry("set_thread_area", kHex),                              // 205
    MakeEntry("io_setup", kInt, kHex),                               // 206
    MakeEntry("io_destroy", kInt),                                   // 207
    MakeEntry("io_getevents", kInt, kInt, kInt, kHex, kHex),         // 208
    MakeEntry("io_submit", kInt, kInt, kHex),                        // 209
    MakeEntry("io_cancel", kInt, kHex, kHex),                        // 210
    MakeEntry("get_thread_area", kHex),                              // 211
    MakeEntry("lookup_dcookie", kInt, kString, kInt),                // 212
    MakeEntry("epoll_create", kInt),                                 // 213
    MakeEntry("epoll_ctl_old", UnknownArguments()),                  // 214
    MakeEntry("epoll_wait_old", UnknownArguments()),                 // 215
    MakeEntry("remap_file_pages", kGen, kInt, kInt, kInt, kHex),     // 216
    MakeEntry("getdents64", kInt, kHex, kInt),                       // 217
    MakeEntry("set_tid_address", kHex),                              // 218
    MakeEntry("restart_syscall"),                                    // 219
    MakeEntry("semtimedop", kInt, kHex, kInt, kHex),                 // 220
    MakeEntry("fadvise64", kInt, kInt, kInt, kInt),                  // 221
    MakeEntry("timer_create", kInt, kHex, kHex),                     // 222
    MakeEntry("timer_settime", kInt, kHex, kHex, kHex),              // 223
    MakeEntry("timer_gettime", kInt, kHex),                          // 224
    MakeEntry("timer_getoverrun", kInt),                             // 225
    MakeEntry("timer_delete", kInt),                                 // 226
    MakeEntry("clock_settime", kInt, kHex),                          // 227
    MakeEntry("clock_gettime", kInt, kHex),                          // 228
    MakeEntry("clock_getres", kInt, kHex),                           // 229
    MakeEntry("clock_nanosleep", kInt, kHex, kHex, kHex),            // 230
    MakeEntry("exit_group", kInt),                                   // 231
    MakeEntry("epoll_wait", kInt, kHex, kInt, kInt),                 // 232
    MakeEntry("epoll_ctl", kInt, kInt, kInt, kHex),                  // 233
    MakeEntry("tgkill", kInt, kInt, kSignal),                        // 234
    MakeEntry("utimes", kPath, kHex),                                // 235
    MakeEntry("vserver", UnknownArguments()),                        // 236
    MakeEntry("mbind", kGen, kInt, kInt, kHex, kInt, kHex),          // 237
    MakeEntry("set_mempolicy", kInt, kHex, kInt),                    // 238
    MakeEntry("get_mempolicy", kInt, kHex, kInt, kInt, kHex),        // 239
    MakeEntry("mq_open", kString, kHex, kOct, kHex),                 // 240
    MakeEntry("mq_unlink", kString),                                 // 241
    MakeEntry("mq_timedsend", kHex, kHex, kInt, kInt, kHex),         // 242
    MakeEntry("mq_timedreceive", kHex, kHex, kInt, kHex, kHex),      // 243
    MakeEntry("mq_notify", kHex, kHex),                              // 244
    MakeEntry("mq_getsetattr", kHex, kHex, kHex),                    // 245
    MakeEntry("kexec_load", kHex, kInt, kHex, kHex),                 // 246
    MakeEntry("waitid", kInt, kInt, kHex, kInt, kHex),               // 247
    MakeEntry("add_key", kString, kString, kGen, kInt, kInt),        // 248
    MakeEntry("request_key", kString, kString, kHex, kInt),          // 249
    MakeEntry("keyctl", kInt, kInt, kInt, kInt, kInt),               // 250
    MakeEntry("ioprio_set", kInt, kInt, kInt),                       // 251
    MakeEntry("ioprio_get", kInt, kInt),                             // 252
    MakeEntry("inotify_init"),                                       // 253
    MakeEntry("inotify_add_watch", kInt, kPath, kHex),               // 254
    MakeEntry("inotify_rm_watch", kInt, kInt),                       // 255
    MakeEntry("migrate_pages", kInt, kInt, kHex, kHex),              // 256
    MakeEntry("openat", kInt, kPath, kHex, kOct),                    // 257
    MakeEntry("mkdirat", kInt, kPath, kOct),                         // 258
    MakeEntry("mknodat", kInt, kPath, kOct, kHex),                   // 259
    MakeEntry("fchownat", kInt, kPath, kInt, kInt, kHex),            // 260
    MakeEntry("futimesat", kInt, kPath, kHex),                       // 261
    MakeEntry("newfstatat", kInt, kPath, kHex, kHex),                // 262
    MakeEntry("unlinkat", kInt, kPath, kHex),                        // 263
    MakeEntry("renameat", kInt, kPath, kInt, kPath),                 // 264
    MakeEntry("linkat", kInt, kPath, kInt, kPath, kHex),             // 265
    MakeEntry("symlinkat", kPath, kInt, kPath),                      // 266
    MakeEntry("readlinkat", kInt, kPath, kHex, kInt),                // 267
    MakeEntry("fchmodat", kInt, kPath, kOct),                        // 268
    MakeEntry("faccessat", kInt, kPath, kInt, kHex),                 // 269
    MakeEntry("pselect6", kInt, kHex, kHex, kHex, kHex),             // 270
    MakeEntry("ppoll", kHex, kInt, kHex, kHex, kInt),                // 271
    MakeEntry("unshare", kHex),                                      // 272
    MakeEntry("set_robust_list", kHex, kInt),                        // 273
    MakeEntry("get_robust_list", kInt, kHex, kHex),                  // 274
    MakeEntry("splice", kInt, kHex, kInt, kHex, kInt, kHex),         // 275
    MakeEntry("tee", kInt, kInt, kInt, kHex),                        // 276
    MakeEntry("sync_file_range", kInt, kInt, kInt, kHex),            // 277
    MakeEntry("vmsplice", kInt, kHex, kInt, kInt),                   // 278
    MakeEntry("move_pages", kInt, kInt, kHex, kHex, kHex, kHex),     // 279
    MakeEntry("utimensat", kInt, kPath, kHex, kHex),                 // 280
    MakeEntry("epoll_pwait", kInt, kHex, kInt, kInt, kHex, kInt),    // 281
    MakeEntry("signalfd", kInt, kHex, kHex),                         // 282
    MakeEntry("timerfd_create", kInt, kHex),                         // 283
    MakeEntry("eventfd", kInt),                                      // 284
    MakeEntry("fallocate", kInt, kOct, kInt, kInt),                  // 285
    MakeEntry("timerfd_settime", kInt, kHex, kHex, kHex),            // 286
    MakeEntry("timerfd_gettime", kInt, kHex),                        // 287
    MakeEntry("accept4", kInt, kHex, kHex, kInt),                    // 288
    MakeEntry("signalfd4", kInt, kHex, kHex, kHex),                  // 289
    MakeEntry("eventfd2", kInt, kHex),                               // 290
    MakeEntry("epoll_create1", kHex),                                // 291
    MakeEntry("dup3", kInt, kInt, kHex),                             // 292
    MakeEntry("pipe2", kHex, kHex),                                  // 293
    MakeEntry("inotify_init1", kHex),                                // 294
    MakeEntry("preadv", kInt, kHex, kInt, kInt, kInt),               // 295
    MakeEntry("pwritev", kInt, kHex, kInt, kInt, kInt),              // 296
    MakeEntry("rt_tgsigqueueinfo", kInt, kInt, kInt, kHex),          // 297
    MakeEntry("perf_event_open", kHex, kInt, kInt, kInt, kHex),      // 298
    MakeEntry("recvmmsg", kInt, kHex, kInt, kHex, kHex),             // 299
    MakeEntry("fanotify_init", kHex, kHex),                          // 300
    MakeEntry("fanotify_mark", kInt, kHex, kHex, kInt, kPath),       // 301
    MakeEntry("prlimit64", kInt, kInt, kHex, kHex),                  // 302
    MakeEntry("name_to_handle_at", kInt, kPath, kHex, kHex, kHex),   // 303
    MakeEntry("open_by_handle_at", kInt, kHex, kHex),                // 304
    MakeEntry("clock_adjtime", kInt, kHex),                          // 305
    MakeEntry("syncfs", kInt),                                       // 306
    MakeEntry("sendmmsg", kInt, kHex, kInt, kHex),                   // 307
    MakeEntry("setns", kInt, kHex),                                  // 308
    MakeEntry("getcpu", kHex, kHex, kHex),                           // 309
    MakeEntry("process_vm_readv", kInt, kHex, kInt, kHex, kInt,
              kInt),  // 310
    MakeEntry("process_vm_writev", kInt, kHex, kInt, kHex, kInt,
              kInt),                                                // 311
    MakeEntry("kcmp", kInt, kInt, kInt, kInt, kInt),                // 312
    MakeEntry("finit_module", kInt, kString, kHex),                 // 313
    MakeEntry("sched_setattr", kInt, kHex, kHex),                   // 314
    MakeEntry("sched_getattr", kInt, kHex, kInt, kHex),             // 315
    MakeEntry("renameat2", kInt, kPath, kInt, kPath, kHex),         // 316
    MakeEntry("seccomp", kInt, kHex, kHex),                         // 317
    MakeEntry("getrandom", kGen, kInt, kHex),                       // 318
    MakeEntry("memfd_create", kString, kHex),                       // 319
    MakeEntry("kexec_file_load", kInt, kInt, kInt, kString, kHex),  // 320
    MakeEntry("bpf", kInt, kHex, kInt),                             // 321
    MakeEntry("execveat", kInt, kPath, kHex, kHex, kHex),           // 322
    MakeEntry("userfaultfd", kHex),                                 // 323
    MakeEntry("membarrier", kInt, kHex),                            // 324
    MakeEntry("mlock2", kHex, kInt, kHex),                          // 325
    MakeEntry("copy_file_range", kInt, kHex, kInt, kHex, kInt,
              kHex),                                            // 326
    MakeEntry("preadv2", kInt, kHex, kInt, kInt, kInt, kHex),   // 327
    MakeEntry("pwritev2", kInt, kHex, kInt, kInt, kInt, kHex),  // 328
    MakeEntry("pkey_mprotect", kInt, kInt, kHex, kInt),         // 329
    MakeEntry("pkey_alloc", kInt, kInt),                        // 330
    MakeEntry("pkey_free", kInt),                               // 331
    MakeEntry("statx", kInt, kPath, kHex, kHex, kHex),          // 332
};

constexpr std::array kSyscallDataX8632 = {
    MakeEntry("restart_syscall", kHex, kHex, kHex, kHex, kHex, kHex),     // 0
    MakeEntry("exit", kHex, kHex, kHex, kHex, kHex, kHex),                // 1
    MakeEntry("fork", kHex, kHex, kHex, kHex, kHex, kHex),                // 2
    MakeEntry("read", kHex, kHex, kHex, kHex, kHex, kHex),                // 3
    MakeEntry("write", kHex, kHex, kHex, kHex, kHex, kHex),               // 4
    MakeEntry("open", kPath, kHex, kOct, kHex, kHex, kHex),               // 5
    MakeEntry("close", kHex, kHex, kHex, kHex, kHex, kHex),               // 6
    MakeEntry("waitpid", kHex, kHex, kHex, kHex, kHex, kHex),             // 7
    MakeEntry("creat", kPath, kHex, kHex, kHex, kHex, kHex),              // 8
    MakeEntry("link", kPath, kPath, kHex, kHex, kHex, kHex),              // 9
    MakeEntry("unlink", kPath, kHex, kHex, kHex, kHex, kHex),             // 10
    MakeEntry("execve", kPath, kHex, kHex, kHex, kHex, kHex),             // 11
    MakeEntry("chdir", kPath, kHex, kHex, kHex, kHex, kHex),              // 12
    MakeEntry("time", kHex, kHex, kHex, kHex, kHex, kHex),                // 13
    MakeEntry("mknod", kPath, kOct, kHex, kHex, kHex, kHex),              // 14
    MakeEntry("chmod", kPath, kOct, kHex, kHex, kHex, kHex),              // 15
    MakeEntry("lchown", kPath, kInt, kInt, kHex, kHex, kHex),             // 16
    MakeEntry("break", kHex, kHex, kHex, kHex, kHex, kHex),               // 17
    MakeEntry("oldstat", kHex, kHex, kHex, kHex, kHex, kHex),             // 18
    MakeEntry("lseek", kHex, kHex, kHex, kHex, kHex, kHex),               // 19
    MakeEntry("getpid", kHex, kHex, kHex, kHex, kHex, kHex),              // 20
    MakeEntry("mount", kHex, kHex, kHex, kHex, kHex, kHex),               // 21
    MakeEntry("umount", kHex, kHex, kHex, kHex, kHex, kHex),              // 22
    MakeEntry("setuid", kHex, kHex, kHex, kHex, kHex, kHex),              // 23
    MakeEntry("getuid", kHex, kHex, kHex, kHex, kHex, kHex),              // 24
    MakeEntry("stime", kHex, kHex, kHex, kHex, kHex, kHex),               // 25
    MakeEntry("ptrace", kHex, kHex, kHex, kHex),                          // 26
    MakeEntry("alarm", kHex, kHex, kHex, kHex, kHex, kHex),               // 27
    MakeEntry("oldfstat", kHex, kHex, kHex, kHex, kHex, kHex),            // 28
    MakeEntry("pause", kHex, kHex, kHex, kHex, kHex, kHex),               // 29
    MakeEntry("utime", kHex, kHex, kHex, kHex, kHex, kHex),               // 30
    MakeEntry("stty", kHex, kHex, kHex, kHex, kHex, kHex),                // 31
    MakeEntry("gtty", kHex, kHex, kHex, kHex, kHex, kHex),                // 32
    MakeEntry("access", kPath, kHex, kHex, kHex, kHex, kHex),             // 33
    MakeEntry("nice", kHex, kHex, kHex, kHex, kHex, kHex),                // 34
    MakeEntry("ftime", kHex, kHex, kHex, kHex, kHex, kHex),               // 35
    MakeEntry("sync", kHex, kHex, kHex, kHex, kHex, kHex),                // 36
    MakeEntry("kill", kHex, kHex, kHex, kHex, kHex, kHex),                // 37
    MakeEntry("rename", kPath, kPath, kHex, kHex, kHex, kHex),            // 38
    MakeEntry("mkdir", kPath, kHex, kHex, kHex, kHex, kHex),              // 39
    MakeEntry("rmdir", kHex, kHex, kHex, kHex, kHex, kHex),               // 40
    MakeEntry("dup", kHex, kHex, kHex, kHex, kHex, kHex),                 // 41
    MakeEntry("pipe", kHex, kHex, kHex, kHex, kHex, kHex),                // 42
    MakeEntry("times", kHex, kHex, kHex, kHex, kHex, kHex),               // 43
    MakeEntry("prof", kHex, kHex, kHex, kHex, kHex, kHex),                // 44
    MakeEntry("brk", kHex, kHex, kHex, kHex, kHex, kHex),                 // 45
    MakeEntry("setgid", kHex, kHex, kHex, kHex, kHex, kHex),              // 46
    MakeEntry("getgid", kHex, kHex, kHex, kHex, kHex, kHex),              // 47
    MakeEntry("signal", kHex, kHex, kHex, kHex, kHex, kHex),              // 48
    MakeEntry("geteuid", kHex, kHex, kHex, kHex, kHex, kHex),             // 49
    MakeEntry("getegid", kHex, kHex, kHex, kHex, kHex, kHex),             // 50
    MakeEntry("acct", kHex, kHex, kHex, kHex, kHex, kHex),                // 51
    MakeEntry("umount2", kHex, kHex, kHex, kHex, kHex, kHex),             // 52
    MakeEntry("lock", kHex, kHex, kHex, kHex, kHex, kHex),                // 53
    MakeEntry("ioctl", kHex, kHex, kHex, kHex, kHex, kHex),               // 54
    MakeEntry("fcntl", kHex, kHex, kHex, kHex, kHex, kHex),               // 55
    MakeEntry("mpx", kHex, kHex, kHex, kHex, kHex, kHex),                 // 56
    MakeEntry("setpgid", kHex, kHex, kHex, kHex, kHex, kHex),             // 57
    MakeEntry("ulimit", kHex, kHex, kHex, kHex, kHex, kHex),              // 58
    MakeEntry("oldolduname", kHex, kHex, kHex, kHex, kHex, kHex),         // 59
    MakeEntry("umask", kHex, kHex, kHex, kHex, kHex, kHex),               // 60
    MakeEntry("chroot", kHex, kHex, kHex, kHex, kHex, kHex),              // 61
    MakeEntry("ustat", kHex, kHex, kHex, kHex, kHex, kHex),               // 62
    MakeEntry("dup2", kHex, kHex, kHex, kHex, kHex, kHex),                // 63
    MakeEntry("getppid", kHex, kHex, kHex, kHex, kHex, kHex),             // 64
    MakeEntry("getpgrp", kHex, kHex, kHex, kHex, kHex, kHex),             // 65
    MakeEntry("setsid", kHex, kHex, kHex, kHex, kHex, kHex),              // 66
    MakeEntry("sigaction", kHex, kHex, kHex, kHex, kHex, kHex),           // 67
    MakeEntry("sgetmask", kHex, kHex, kHex, kHex, kHex, kHex),            // 68
    MakeEntry("ssetmask", kHex, kHex, kHex, kHex, kHex, kHex),            // 69
    MakeEntry("setreuid", kHex, kHex, kHex, kHex, kHex, kHex),            // 70
    MakeEntry("setregid", kHex, kHex, kHex, kHex, kHex, kHex),            // 71
    MakeEntry("sigsuspend", kHex, kHex, kHex, kHex, kHex, kHex),          // 72
    MakeEntry("sigpending", kHex, kHex, kHex, kHex, kHex, kHex),          // 73
    MakeEntry("sethostname", kHex, kHex, kHex, kHex, kHex, kHex),         // 74
    MakeEntry("setrlimit", kHex, kHex, kHex, kHex, kHex, kHex),           // 75
    MakeEntry("getrlimit", kHex, kHex, kHex, kHex, kHex, kHex),           // 76
    MakeEntry("getrusage", kHex, kHex, kHex, kHex, kHex, kHex),           // 77
    MakeEntry("gettimeofday", kHex, kHex, kHex, kHex, kHex, kHex),        // 78
    MakeEntry("settimeofday", kHex, kHex, kHex, kHex, kHex, kHex),        // 79
    MakeEntry("getgroups", kHex, kHex, kHex, kHex, kHex, kHex),           // 80
    MakeEntry("setgroups", kHex, kHex, kHex, kHex, kHex, kHex),           // 81
    MakeEntry("select", kHex, kHex, kHex, kHex, kHex, kHex),              // 82
    MakeEntry("symlink", kPath, kPath, kHex, kHex, kHex, kHex),           // 83
    MakeEntry("oldlstat", kHex, kHex, kHex, kHex, kHex, kHex),            // 84
    MakeEntry("readlink", kPath, kHex, kInt, kHex, kHex, kHex),           // 85
    MakeEntry("uselib", kPath, kHex, kHex, kHex, kHex, kHex),             // 86
    MakeEntry("swapon", kHex, kHex, kHex, kHex, kHex, kHex),              // 87
    MakeEntry("reboot", kHex, kHex, kHex, kHex, kHex, kHex),              // 88
    MakeEntry("readdir", kHex, kHex, kHex, kHex, kHex, kHex),             // 89
    MakeEntry("mmap", kHex, kHex, kHex, kHex, kHex, kHex),                // 90
    MakeEntry("munmap", kHex, kHex, kHex, kHex, kHex, kHex),              // 91
    MakeEntry("truncate", kPath, kHex, kHex, kHex, kHex, kHex),           // 92
    MakeEntry("ftruncate", kHex, kHex, kHex, kHex, kHex, kHex),           // 93
    MakeEntry("fchmod", kHex, kHex, kHex, kHex, kHex, kHex),              // 94
    MakeEntry("fchown", kHex, kHex, kHex, kHex, kHex, kHex),              // 95
    MakeEntry("getpriority", kHex, kHex, kHex, kHex, kHex, kHex),         // 96
    MakeEntry("setpriority", kHex, kHex, kHex, kHex, kHex, kHex),         // 97
    MakeEntry("profil", kHex, kHex, kHex, kHex, kHex, kHex),              // 98
    MakeEntry("statfs", kPath, kHex, kHex, kHex, kHex, kHex),             // 99
    MakeEntry("fstatfs", kHex, kHex, kHex, kHex, kHex, kHex),             // 100
    MakeEntry("ioperm", kHex, kHex, kHex, kHex, kHex, kHex),              // 101
    MakeEntry("socketcall", kHex, kHex, kHex, kHex, kHex, kHex),          // 102
    MakeEntry("syslog", kHex, kHex, kHex, kHex, kHex, kHex),              // 103
    MakeEntry("setitimer", kHex, kHex, kHex, kHex, kHex, kHex),           // 104
    MakeEntry("getitimer", kHex, kHex, kHex, kHex, kHex, kHex),           // 105
    MakeEntry("stat", kPath, kHex, kHex, kHex, kHex, kHex),               // 106
    MakeEntry("lstat", kPath, kHex, kHex, kHex, kHex, kHex),              // 107
    MakeEntry("fstat", kHex, kHex, kHex, kHex, kHex, kHex),               // 108
    MakeEntry("olduname", kHex, kHex, kHex, kHex, kHex, kHex),            // 109
    MakeEntry("iopl", kHex, kHex, kHex, kHex, kHex, kHex),                // 110
    MakeEntry("vhangup", kHex, kHex, kHex, kHex, kHex, kHex),             // 111
    MakeEntry("idle", kHex, kHex, kHex, kHex, kHex, kHex),                // 112
    MakeEntry("vm86old", kHex, kHex, kHex, kHex, kHex, kHex),             // 113
    MakeEntry("wait4", kHex, kHex, kHex, kHex, kHex, kHex),               // 114
    MakeEntry("swapoff", kHex, kHex, kHex, kHex, kHex, kHex),             // 115
    MakeEntry("sysinfo", kHex, kHex, kHex, kHex, kHex, kHex),             // 116
    MakeEntry("ipc", kHex, kHex, kHex, kHex, kHex, kHex),                 // 117
    MakeEntry("fsync", kHex, kHex, kHex, kHex, kHex, kHex),               // 118
    MakeEntry("sigreturn", kHex, kHex, kHex, kHex, kHex, kHex),           // 119
    MakeEntry("clone", kHex, kHex, kHex, kHex, kHex, kHex),               // 120
    MakeEntry("setdomainname", kHex, kHex, kHex, kHex, kHex, kHex),       // 121
    MakeEntry("uname", kHex, kHex, kHex, kHex, kHex, kHex),               // 122
    MakeEntry("modify_ldt", kHex, kHex, kHex, kHex, kHex, kHex),          // 123
    MakeEntry("adjtimex", kHex, kHex, kHex, kHex, kHex, kHex),            // 124
    MakeEntry("mprotect", kHex, kHex, kHex, kHex, kHex, kHex),            // 125
    MakeEntry("sigprocmask", kHex, kHex, kHex, kHex, kHex, kHex),         // 126
    MakeEntry("create_module", kHex, kHex, kHex, kHex, kHex, kHex),       // 127
    MakeEntry("init_module", kHex, kHex, kHex, kHex, kHex, kHex),         // 128
    MakeEntry("delete_module", kHex, kHex, kHex, kHex, kHex, kHex),       // 129
    MakeEntry("get_kernel_syms", kHex, kHex, kHex, kHex, kHex, kHex),     // 130
    MakeEntry("quotactl", kHex, kHex, kHex, kHex, kHex, kHex),            // 131
    MakeEntry("getpgid", kHex, kHex, kHex, kHex, kHex, kHex),             // 132
    MakeEntry("fchdir", kHex, kHex, kHex, kHex, kHex, kHex),              // 133
    MakeEntry("bdflush", kHex, kHex, kHex, kHex, kHex, kHex),             // 134
    MakeEntry("sysfs", kHex, kHex, kHex, kHex, kHex, kHex),               // 135
    MakeEntry("personality", kHex, kHex, kHex, kHex, kHex, kHex),         // 136
    MakeEntry("afs_syscall", kHex, kHex, kHex, kHex, kHex, kHex),         // 137
    MakeEntry("setfsuid", kHex, kHex, kHex, kHex, kHex, kHex),            // 138
    MakeEntry("setfsgid", kHex, kHex, kHex, kHex, kHex, kHex),            // 139
    MakeEntry("_llseek", kHex, kHex, kHex, kHex, kHex, kHex),             // 140
    MakeEntry("getdents", kHex, kHex, kHex, kHex, kHex, kHex),            // 141
    MakeEntry("_newselect", kHex, kHex, kHex, kHex, kHex, kHex),          // 142
    MakeEntry("flock", kHex, kHex, kHex, kHex, kHex, kHex),               // 143
    MakeEntry("msync", kHex, kHex, kHex, kHex, kHex, kHex),               // 144
    MakeEntry("readv", kHex, kHex, kHex, kHex, kHex, kHex),               // 145
    MakeEntry("writev", kHex, kHex, kHex, kHex, kHex, kHex),              // 146
    MakeEntry("getsid", kHex, kHex, kHex, kHex, kHex, kHex),              // 147
    MakeEntry("fdatasync", kHex, kHex, kHex, kHex, kHex, kHex),           // 148
    MakeEntry("_sysctl", kHex, kHex, kHex, kHex, kHex, kHex),             // 149
    MakeEntry("mlock", kHex, kHex, kHex, kHex, kHex, kHex),               // 150
    MakeEntry("munlock", kHex, kHex, kHex, kHex, kHex, kHex),             // 151
    MakeEntry("mlockall", kHex, kHex, kHex, kHex, kHex, kHex),            // 152
    MakeEntry("munlockall", kHex, kHex, kHex, kHex, kHex, kHex),          // 153
    MakeEntry("sched_setparam", kHex, kHex, kHex, kHex, kHex, kHex),      // 154
    MakeEntry("sched_getparam", kHex, kHex, kHex, kHex, kHex, kHex),      // 155
    MakeEntry("sched_setscheduler", kHex, kHex, kHex, kHex, kHex, kHex),  // 156
    MakeEntry("sched_getscheduler", kHex, kHex, kHex, kHex, kHex, kHex),  // 157
    MakeEntry("sched_yield", kHex, kHex, kHex, kHex, kHex, kHex),         // 158
    MakeEntry("sched_get_priority_max", kHex, kHex, kHex, kHex, kHex,
              kHex),  // 159
    MakeEntry("sched_get_priority_min", kHex, kHex, kHex, kHex, kHex,
              kHex),  // 160
    MakeEntry("sched_rr_get_interval", kHex, kHex, kHex, kHex, kHex,
              kHex),                                                     // 161
    MakeEntry("nanosleep", kHex, kHex, kHex, kHex, kHex, kHex),          // 162
    MakeEntry("mremap", kHex, kHex, kHex, kHex, kHex, kHex),             // 163
    MakeEntry("setresuid", kHex, kHex, kHex, kHex, kHex, kHex),          // 164
    MakeEntry("getresuid", kHex, kHex, kHex, kHex, kHex, kHex),          // 165
    MakeEntry("vm86", kHex, kHex, kHex, kHex, kHex, kHex),               // 166
    MakeEntry("query_module", kHex, kHex, kHex, kHex, kHex, kHex),       // 167
    MakeEntry("poll", kHex, kHex, kHex, kHex, kHex, kHex),               // 168
    MakeEntry("nfsservctl", kHex, kHex, kHex, kHex, kHex, kHex),         // 169
    MakeEntry("setresgid", kHex, kHex, kHex, kHex, kHex, kHex),          // 170
    MakeEntry("getresgid", kHex, kHex, kHex, kHex, kHex, kHex),          // 171
    MakeEntry("prctl", kHex, kHex, kHex, kHex, kHex, kHex),              // 172
    MakeEntry("rt_sigreturn", kHex, kHex, kHex, kHex, kHex, kHex),       // 173
    MakeEntry("rt_sigaction", kHex, kHex, kHex, kHex, kHex, kHex),       // 174
    MakeEntry("rt_sigprocmask", kHex, kHex, kHex, kHex, kHex, kHex),     // 175
    MakeEntry("rt_sigpending", kHex, kHex, kHex, kHex, kHex, kHex),      // 176
    MakeEntry("rt_sigtimedwait", kHex, kHex, kHex, kHex, kHex, kHex),    // 177
    MakeEntry("rt_sigqueueinfo", kHex, kHex, kHex, kHex, kHex, kHex),    // 178
    MakeEntry("rt_sigsuspend", kHex, kHex, kHex, kHex, kHex, kHex),      // 179
    MakeEntry("pread64", kHex, kHex, kHex, kHex, kHex, kHex),            // 180
    MakeEntry("pwrite64", kHex, kHex, kHex, kHex, kHex, kHex),           // 181
    MakeEntry("chown", kHex, kHex, kHex, kHex, kHex, kHex),              // 182
    MakeEntry("getcwd", kHex, kHex, kHex, kHex, kHex, kHex),             // 183
    MakeEntry("capget", kHex, kHex, kHex, kHex, kHex, kHex),             // 184
    MakeEntry("capset", kHex, kHex, kHex, kHex, kHex, kHex),             // 185
    MakeEntry("sigaltstack", kHex, kHex, kHex, kHex, kHex, kHex),        // 186
    MakeEntry("sendfile", kHex, kHex, kHex, kHex, kHex, kHex),           // 187
    MakeEntry("getpmsg", kHex, kHex, kHex, kHex, kHex, kHex),            // 188
    MakeEntry("putpmsg", kHex, kHex, kHex, kHex, kHex, kHex),            // 189
    MakeEntry("vfork", kHex, kHex, kHex, kHex, kHex, kHex),              // 190
    MakeEntry("ugetrlimit", kHex, kHex, kHex, kHex, kHex, kHex),         // 191
    MakeEntry("mmap2", kHex, kHex, kHex, kHex, kHex, kHex),              // 192
    MakeEntry("truncate64", kPath, kHex, kHex, kHex, kHex, kHex),        // 193
    MakeEntry("ftruncate64", kHex, kHex, kHex, kHex, kHex, kHex),        // 194
    MakeEntry("stat64", kHex, kHex, kHex, kHex, kHex, kHex),             // 195
    MakeEntry("lstat64", kPath, kHex, kHex, kHex, kHex, kHex),           // 196
    MakeEntry("fstat64", kHex, kHex, kHex, kHex, kHex, kHex),            // 197
    MakeEntry("lchown32", kHex, kHex, kHex, kHex, kHex, kHex),           // 198
    MakeEntry("getuid32", kHex, kHex, kHex, kHex, kHex, kHex),           // 199
    MakeEntry("getgid32", kHex, kHex, kHex, kHex, kHex, kHex),           // 200
    MakeEntry("geteuid32", kHex, kHex, kHex, kHex, kHex, kHex),          // 201
    MakeEntry("getegid32", kHex, kHex, kHex, kHex, kHex, kHex),          // 202
    MakeEntry("setreuid32", kHex, kHex, kHex, kHex, kHex, kHex),         // 203
    MakeEntry("setregid32", kHex, kHex, kHex, kHex, kHex, kHex),         // 204
    MakeEntry("getgroups32", kHex, kHex, kHex, kHex, kHex, kHex),        // 205
    MakeEntry("setgroups32", kHex, kHex, kHex, kHex, kHex, kHex),        // 206
    MakeEntry("fchown32", kHex, kHex, kHex, kHex, kHex, kHex),           // 207
    MakeEntry("setresuid32", kHex, kHex, kHex, kHex, kHex, kHex),        // 208
    MakeEntry("getresuid32", kHex, kHex, kHex, kHex, kHex, kHex),        // 209
    MakeEntry("setresgid32", kHex, kHex, kHex, kHex, kHex, kHex),        // 210
    MakeEntry("getresgid32", kHex, kHex, kHex, kHex, kHex, kHex),        // 211
    MakeEntry("chown32", kHex, kHex, kHex, kHex, kHex, kHex),            // 212
    MakeEntry("setuid32", kHex, kHex, kHex, kHex, kHex, kHex),           // 213
    MakeEntry("setgid32", kHex, kHex, kHex, kHex, kHex, kHex),           // 214
    MakeEntry("setfsuid32", kHex, kHex, kHex, kHex, kHex, kHex),         // 215
    MakeEntry("setfsgid32", kHex, kHex, kHex, kHex, kHex, kHex),         // 216
    MakeEntry("pivot_root", kHex, kHex, kHex, kHex, kHex, kHex),         // 217
    MakeEntry("mincore", kHex, kHex, kHex, kHex, kHex, kHex),            // 218
    MakeEntry("madvise", kHex, kHex, kHex, kHex, kHex, kHex),            // 219
    MakeEntry("getdents64", kHex, kHex, kHex, kHex, kHex, kHex),         // 220
    MakeEntry("fcntl64", kHex, kHex, kHex, kHex, kHex, kHex),            // 221
    MakeEntry("unused1-222", kHex, kHex, kHex, kHex, kHex, kHex),        // 222
    MakeEntry("unused2-223", kHex, kHex, kHex, kHex, kHex, kHex),        // 223
    MakeEntry("gettid", kHex, kHex, kHex, kHex, kHex, kHex),             // 224
    MakeEntry("readahead", kHex, kHex, kHex, kHex, kHex, kHex),          // 225
    MakeEntry("setxattr", kHex, kHex, kHex, kHex, kHex, kHex),           // 226
    MakeEntry("lsetxattr", kHex, kHex, kHex, kHex, kHex, kHex),          // 227
    MakeEntry("fsetxattr", kHex, kHex, kHex, kHex, kHex, kHex),          // 228
    MakeEntry("getxattr", kHex, kHex, kHex, kHex, kHex, kHex),           // 229
    MakeEntry("lgetxattr", kHex, kHex, kHex, kHex, kHex, kHex),          // 230
    MakeEntry("fgetxattr", kHex, kHex, kHex, kHex, kHex, kHex),          // 231
    MakeEntry("listxattr", kHex, kHex, kHex, kHex, kHex, kHex),          // 232
    MakeEntry("llistxattr", kHex, kHex, kHex, kHex, kHex, kHex),         // 233
    MakeEntry("flistxattr", kHex, kHex, kHex, kHex, kHex, kHex),         // 234
    MakeEntry("removexattr", kHex, kHex, kHex, kHex, kHex, kHex),        // 235
    MakeEntry("lremovexattr", kHex, kHex, kHex, kHex, kHex, kHex),       // 236
    MakeEntry("fremovexattr", kHex, kHex, kHex, kHex, kHex, kHex),       // 237
    MakeEntry("tkill", kHex, kHex, kHex, kHex, kHex, kHex),              // 238
    MakeEntry("sendfile64", kHex, kHex, kHex, kHex, kHex, kHex),         // 239
    MakeEntry("futex", kHex, kHex, kHex, kHex, kHex, kHex),              // 240
    MakeEntry("sched_setaffinity", kHex, kHex, kHex, kHex, kHex, kHex),  // 241
    MakeEntry("sched_getaffinity", kHex, kHex, kHex, kHex, kHex, kHex),  // 242
    MakeEntry("set_thread_area", kHex, kHex, kHex, kHex, kHex, kHex),    // 243
    MakeEntry("get_thread_area", kHex, kHex, kHex, kHex, kHex, kHex),    // 244
    MakeEntry("io_setup", kHex, kHex, kHex, kHex, kHex, kHex),           // 245
    MakeEntry("io_destroy", kHex, kHex, kHex, kHex, kHex, kHex),         // 246
    MakeEntry("io_getevents", kHex, kHex, kHex, kHex, kHex, kHex),       // 247
    MakeEntry("io_submit", kHex, kHex, kHex, kHex, kHex, kHex),          // 248
    MakeEntry("io_cancel", kHex, kHex, kHex, kHex, kHex, kHex),          // 249
    MakeEntry("fadvise64", kHex, kHex, kHex, kHex, kHex, kHex),          // 250
    MakeEntry("251-old_sys_set_zone_reclaim", kHex, kHex, kHex, kHex, kHex,
              kHex),                                                    // 251
    MakeEntry("exit_group", kHex, kHex, kHex, kHex, kHex, kHex),        // 252
    MakeEntry("lookup_dcookie", kHex, kHex, kHex, kHex, kHex, kHex),    // 253
    MakeEntry("epoll_create", kHex, kHex, kHex, kHex, kHex, kHex),      // 254
    MakeEntry("epoll_ctl", kHex, kHex, kHex, kHex, kHex, kHex),         // 255
    MakeEntry("epoll_wait", kHex, kHex, kHex, kHex, kHex, kHex),        // 256
    MakeEntry("remap_file_pages", kHex, kHex, kHex, kHex, kHex, kHex),  // 257
    MakeEntry("set_tid_address", kHex, kHex, kHex, kHex, kHex, kHex),   // 258
    MakeEntry("timer_create", kHex, kHex, kHex, kHex, kHex, kHex),      // 259
    MakeEntry("timer_settime", kHex, kHex, kHex, kHex, kHex, kHex),     // 260
    MakeEntry("timer_gettime", kHex, kHex, kHex, kHex, kHex, kHex),     // 261
    MakeEntry("timer_getoverrun", kHex, kHex, kHex, kHex, kHex, kHex),  // 262
    MakeEntry("timer_delete", kHex, kHex, kHex, kHex, kHex, kHex),      // 263
    MakeEntry("clock_settime", kHex, kHex, kHex, kHex, kHex, kHex),     // 264
    MakeEntry("clock_gettime", kHex, kHex, kHex, kHex, kHex, kHex),     // 265
    MakeEntry("clock_getres", kHex, kHex, kHex, kHex, kHex, kHex),      // 266
    MakeEntry("clock_nanosleep", kHex, kHex, kHex, kHex, kHex, kHex),   // 267
    MakeEntry("statfs64", kHex, kHex, kHex, kHex, kHex, kHex),          // 268
    MakeEntry("fstatfs64", kHex, kHex, kHex, kHex, kHex, kHex),         // 269
    MakeEntry("tgkill", kHex, kHex, kHex, kHex, kHex, kHex),            // 270
    MakeEntry("utimes", kHex, kHex, kHex, kHex, kHex, kHex),            // 271
    MakeEntry("fadvise64_64", kHex, kHex, kHex, kHex, kHex, kHex),      // 272
    MakeEntry("vserver", kHex, kHex, kHex, kHex, kHex, kHex),           // 273
    MakeEntry("mbind", kHex, kHex, kHex, kHex, kHex, kHex),             // 274
    MakeEntry("get_mempolicy", kHex, kHex, kHex, kHex, kHex, kHex),     // 275
    MakeEntry("set_mempolicy", kHex, kHex, kHex, kHex, kHex, kHex),     // 276
    MakeEntry("mq_open", kHex, kHex, kHex, kHex, kHex, kHex),           // 277
    MakeEntry("mq_unlink", kHex, kHex, kHex, kHex, kHex, kHex),         // 278
    MakeEntry("mq_timedsend", kHex, kHex, kHex, kHex, kHex, kHex),      // 279
    MakeEntry("mq_timedreceive", kHex, kHex, kHex, kHex, kHex, kHex),   // 280
    MakeEntry("mq_notify", kHex, kHex, kHex, kHex, kHex, kHex),         // 281
    MakeEntry("mq_getsetattr", kHex, kHex, kHex, kHex, kHex, kHex),     // 282
    MakeEntry("kexec_load", kHex, kHex, kHex, kHex, kHex, kHex),        // 283
    MakeEntry("waitid", kHex, kHex, kHex, kHex, kHex, kHex),            // 284
    MakeEntry("285-old_sys_setaltroot", kHex, kHex, kHex, kHex, kHex,
              kHex),                                                     // 285
    MakeEntry("add_key", kHex, kHex, kHex, kHex, kHex, kHex),            // 286
    MakeEntry("request_key", kHex, kHex, kHex, kHex, kHex, kHex),        // 287
    MakeEntry("keyctl", kHex, kHex, kHex, kHex, kHex, kHex),             // 288
    MakeEntry("ioprio_set", kHex, kHex, kHex, kHex, kHex, kHex),         // 289
    MakeEntry("ioprio_get", kHex, kHex, kHex, kHex, kHex, kHex),         // 290
    MakeEntry("inotify_init", kHex, kHex, kHex, kHex, kHex, kHex),       // 291
    MakeEntry("inotify_add_watch", kHex, kHex, kHex, kHex, kHex, kHex),  // 292
    MakeEntry("inotify_rm_watch", kHex, kHex, kHex, kHex, kHex, kHex),   // 293
    MakeEntry("migrate_pages", kHex, kHex, kHex, kHex, kHex, kHex),      // 294
    MakeEntry("openat", kHex, kPath, kOct, kHex, kHex, kHex),            // 295
    MakeEntry("mkdirat", kHex, kHex, kHex, kHex, kHex, kHex),            // 296
    MakeEntry("mknodat", kHex, kHex, kHex, kHex, kHex, kHex),            // 297
    MakeEntry("fchownat", kHex, kPath, kHex, kHex, kHex, kHex),          // 298
    MakeEntry("futimesat", kHex, kPath, kHex, kHex, kHex, kHex),         // 299
    MakeEntry("fstatat64", kHex, kHex, kHex, kHex, kHex, kHex),          // 300
    MakeEntry("unlinkat", kHex, kPath, kHex, kHex, kHex, kHex),          // 301
    MakeEntry("renameat", kHex, kPath, kHex, kPath, kHex, kHex),         // 302
    MakeEntry("linkat", kHex, kPath, kHex, kPath, kHex, kHex),           // 303
    MakeEntry("symlinkat", kPath, kHex, kPath, kHex, kHex, kHex),        // 304
    MakeEntry("readlinkat", kHex, kPath, kHex, kHex, kHex, kHex),        // 305
    MakeEntry("fchmodat", kHex, kPath, kHex, kHex, kHex, kHex),          // 306
    MakeEntry("faccessat", kHex, kPath, kHex, kHex, kHex, kHex),         // 307
    MakeEntry("pselect6", kHex, kHex, kHex, kHex, kHex, kHex),           // 308
    MakeEntry("ppoll", kHex, kHex, kHex, kHex, kHex, kHex),              // 309
    MakeEntry("unshare", kHex, kHex, kHex, kHex, kHex, kHex),            // 310
    MakeEntry("set_robust_list", kHex, kHex, kHex, kHex, kHex, kHex),    // 311
    MakeEntry("get_robust_list", kHex, kHex, kHex, kHex, kHex, kHex),    // 312
    MakeEntry("splice", kHex, kHex, kHex, kHex, kHex, kHex),             // 313
    MakeEntry("sync_file_range", kHex, kHex, kHex, kHex, kHex, kHex),    // 314
    MakeEntry("tee", kHex, kHex, kHex, kHex, kHex, kHex),                // 315
    MakeEntry("vmsplice", kHex, kHex, kHex, kHex, kHex, kHex),           // 316
    MakeEntry("move_pages", kHex, kHex, kHex, kHex, kHex, kHex),         // 317
    MakeEntry("getcpu", kHex, kHex, kHex, kHex, kHex, kHex),             // 318
    MakeEntry("epoll_pwait", kHex, kHex, kHex, kHex, kHex, kHex),        // 319
    MakeEntry("utimensat", kHex, kHex, kHex, kHex, kHex, kHex),          // 320
    MakeEntry("signalfd", kHex, kHex, kHex, kHex, kHex, kHex),           // 321
    MakeEntry("timerfd_create", kHex, kHex, kHex, kHex, kHex, kHex),     // 322
    MakeEntry("eventfd", kHex, kHex, kHex, kHex, kHex, kHex),            // 323
    MakeEntry("fallocate", kHex, kHex, kHex, kHex, kHex, kHex),          // 324
    MakeEntry("timerfd_settime", kHex, kHex, kHex, kHex, kHex, kHex),    // 325
    MakeEntry("timerfd_gettime", kHex, kHex, kHex, kHex, kHex, kHex),    // 326
    MakeEntry("signalfd4", kHex, kHex, kHex, kHex, kHex, kHex),          // 327
    MakeEntry("eventfd2", kHex, kHex, kHex, kHex, kHex, kHex),           // 328
    MakeEntry("epoll_create1", kHex, kHex, kHex, kHex, kHex, kHex),      // 329
    MakeEntry("dup3", kHex, kHex, kHex, kHex, kHex, kHex),               // 330
    MakeEntry("pipe2", kHex, kHex, kHex, kHex, kHex, kHex),              // 331
    MakeEntry("inotify_init1", kHex, kHex, kHex, kHex, kHex, kHex),      // 332
    MakeEntry("preadv", kHex, kHex, kHex, kHex, kHex, kHex),             // 333
    MakeEntry("pwritev", kHex, kHex, kHex, kHex, kHex, kHex),            // 334
    MakeEntry("rt_tgsigqueueinfo", kHex, kHex, kHex, kHex, kHex, kHex),  // 335
    MakeEntry("perf_event_open", kHex, kHex, kHex, kHex, kHex, kHex),    // 336
    MakeEntry("recvmmsg", kHex, kHex, kHex, kHex, kHex, kHex),           // 337
    MakeEntry("fanotify_init", kHex, kHex, kHex, kHex, kHex, kHex),      // 338
    MakeEntry("fanotify_mark", kHex, kHex, kHex, kHex, kHex, kHex),      // 339
    MakeEntry("prlimit64", kHex, kHex, kHex, kHex, kHex, kHex),          // 340
    MakeEntry("name_to_handle_at", kHex, kHex, kHex, kHex, kHex, kHex),  // 341
    MakeEntry("open_by_handle_at", kHex, kHex, kHex, kHex, kHex, kHex),  // 342
    MakeEntry("clock_adjtime", kHex, kHex, kHex, kHex, kHex, kHex),      // 343
    MakeEntry("syncfs", kHex, kHex, kHex, kHex, kHex, kHex),             // 344
    MakeEntry("sendmmsg", kHex, kHex, kHex, kHex, kHex, kHex),           // 345
    MakeEntry("setns", kHex, kHex, kHex, kHex, kHex, kHex),              // 346
    MakeEntry("process_vm_readv", kHex, kHex, kHex, kHex, kHex, kHex),   // 347
    MakeEntry("process_vm_writev", kHex, kHex, kHex, kHex, kHex, kHex),  // 348
    MakeEntry("kcmp", kHex, kHex, kHex, kHex, kHex, kHex),               // 349
    MakeEntry("finit_module", kHex, kHex, kHex, kHex, kHex, kHex),       // 350
    MakeEntry("sched_setattr", kHex, kHex, kHex, kHex, kHex, kHex),      // 351
    MakeEntry("sched_getattr", kHex, kHex, kHex, kHex, kHex, kHex),      // 352
    MakeEntry("renameat2", kHex, kPath, kHex, kPath, kHex, kHex),        // 353
    MakeEntry("seccomp", kHex, kHex, kHex, kHex, kHex, kHex),            // 354
    MakeEntry("getrandom", kHex, kHex, kHex, kHex, kHex, kHex),          // 355
    MakeEntry("memfd_create", kHex, kHex, kHex, kHex, kHex, kHex),       // 356
    MakeEntry("bpf", kHex, kHex, kHex, kHex, kHex, kHex),                // 357
};

// http://lxr.free-electrons.com/source/arch/powerpc/include/uapi/asm/unistd.h
// Note: PPC64 syscalls can have up to 7 register arguments, but nobody is
// using the 7th argument - probably for x64 compatibility reasons.
constexpr std::array kSyscallDataPPC64LE = {
    MakeEntry("restart_syscall", kGen, kGen, kGen, kGen, kGen, kGen),     // 0
    MakeEntry("exit", kInt, kGen, kGen, kGen, kGen, kGen),                // 1
    MakeEntry("fork", kGen, kGen, kGen, kGen, kGen, kGen),                // 2
    MakeEntry("read", kInt, kHex, kInt),                                  // 3
    MakeEntry("write", kInt, kHex, kInt, kGen, kGen, kGen),               // 4
    MakeEntry("open", kPath, kHex, kOct, kGen, kGen, kGen),               // 5
    MakeEntry("close", kInt, kGen, kGen, kGen, kGen, kGen),               // 6
    MakeEntry("waitpid", kHex, kHex, kHex, kHex, kHex, kHex),             // 7
    MakeEntry("creat", kPath, kOct, kGen, kGen, kGen, kGen),              // 8
    MakeEntry("link", kPath, kPath, kGen, kGen, kGen, kGen),              // 9
    MakeEntry("unlink", kPath, kGen, kGen, kGen, kGen, kGen),             // 10
    MakeEntry("execve", kPath, kHex, kHex, kGen, kGen, kGen),             // 11
    MakeEntry("chdir", kPath, kGen, kGen, kGen, kGen, kGen),              // 12
    MakeEntry("time", kHex, kGen, kGen, kGen, kGen, kGen),                // 13
    MakeEntry("mknod", kPath, kOct, kHex, kGen, kGen, kGen),              // 14
    MakeEntry("chmod", kPath, kOct, kGen, kGen, kGen, kGen),              // 15
    MakeEntry("lchown", kPath, kInt, kInt, kGen, kGen, kGen),             // 16
    MakeEntry("break", kHex, kHex, kHex, kHex, kHex, kHex),               // 17
    MakeEntry("oldstat", kHex, kHex, kHex, kHex, kHex, kHex),             // 18
    MakeEntry("lseek", kGen, kGen, kGen, kGen, kGen, kGen),               // 19
    MakeEntry("getpid", kGen, kGen, kGen, kGen, kGen, kGen),              // 20
    MakeEntry("mount", kPath, kPath, kString, kHex, kGen, kGen),          // 21
    MakeEntry("umount", kHex, kHex, kHex, kHex, kHex, kHex),              // 22
    MakeEntry("setuid", kGen, kGen, kGen, kGen, kGen, kGen),              // 23
    MakeEntry("getuid", kGen, kGen, kGen, kGen, kGen, kGen),              // 24
    MakeEntry("stime", kHex, kHex, kHex, kHex, kHex, kHex),               // 25
    MakeEntry("ptrace", kGen, kGen, kGen, kGen, kGen, kGen),              // 26
    MakeEntry("alarm", kInt, kGen, kGen, kGen, kGen, kGen),               // 27
    MakeEntry("oldfstat", kHex, kHex, kHex, kHex, kHex, kHex),            // 28
    MakeEntry("pause", kGen, kGen, kGen, kGen, kGen, kGen),               // 29
    MakeEntry("utime", kGen, kGen, kGen, kGen, kGen, kGen),               // 30
    MakeEntry("stty", kHex, kHex, kHex, kHex, kHex, kHex),                // 31
    MakeEntry("gtty", kHex, kHex, kHex, kHex, kHex, kHex),                // 32
    MakeEntry("access", kPath, kHex, kGen, kGen, kGen, kGen),             // 33
    MakeEntry("nice", kHex, kHex, kHex, kHex, kHex, kHex),                // 34
    MakeEntry("ftime", kHex, kHex, kHex, kHex, kHex, kHex),               // 35
    MakeEntry("sync", kGen, kGen, kGen, kGen, kGen, kGen),                // 36
    MakeEntry("kill", kInt, kSignal, kGen, kGen, kGen, kGen),             // 37
    MakeEntry("rename", kPath, kPath, kGen, kGen, kGen, kGen),            // 38
    MakeEntry("mkdir", kPath, kOct, kGen, kGen, kGen, kGen),              // 39
    MakeEntry("rmdir", kPath, kGen, kGen, kGen, kGen, kGen),              // 40
    MakeEntry("dup", kGen, kGen, kGen, kGen, kGen, kGen),                 // 41
    MakeEntry("pipe", kGen, kGen, kGen, kGen, kGen, kGen),                // 42
    MakeEntry("times", kGen, kGen, kGen, kGen, kGen, kGen),               // 43
    MakeEntry("prof", kHex, kHex, kHex, kHex, kHex, kHex),                // 44
    MakeEntry("brk", kHex, kGen, kGen, kGen, kGen, kGen),                 // 45
    MakeEntry("setgid", kGen, kGen, kGen, kGen, kGen, kGen),              // 46
    MakeEntry("getgid", kGen, kGen, kGen, kGen, kGen, kGen),              // 47
    MakeEntry("signal", kHex, kHex, kHex, kHex, kHex, kHex),              // 48
    MakeEntry("geteuid", kGen, kGen, kGen, kGen, kGen, kGen),             // 49
    MakeEntry("getegid", kGen, kGen, kGen, kGen, kGen, kGen),             // 50
    MakeEntry("acct", kPath, kGen, kGen, kGen, kGen, kGen),               // 51
    MakeEntry("umount2", kPath, kHex, kGen, kGen, kGen, kGen),            // 52
    MakeEntry("lock", kHex, kHex, kHex, kHex, kHex, kHex),                // 53
    MakeEntry("ioctl", kGen, kGen, kGen, kGen, kGen, kGen),               // 54
    MakeEntry("fcntl", kGen, kGen, kGen, kGen, kGen, kGen),               // 55
    MakeEntry("mpx", kHex, kHex, kHex, kHex, kHex, kHex),                 // 56
    MakeEntry("setpgid", kGen, kGen, kGen, kGen, kGen, kGen),             // 57
    MakeEntry("ulimit", kHex, kHex, kHex, kHex, kHex, kHex),              // 58
    MakeEntry("oldolduname", kHex, kHex, kHex, kHex, kHex, kHex),         // 59
    MakeEntry("umask", kHex, kGen, kGen, kGen, kGen, kGen),               // 60
    MakeEntry("chroot", kPath, kGen, kGen, kGen, kGen, kGen),             // 61
    MakeEntry("ustat", kGen, kGen, kGen, kGen, kGen, kGen),               // 62
    MakeEntry("dup2", kGen, kGen, kGen, kGen, kGen, kGen),                // 63
    MakeEntry("getppid", kGen, kGen, kGen, kGen, kGen, kGen),             // 64
    MakeEntry("getpgrp", kGen, kGen, kGen, kGen, kGen, kGen),             // 65
    MakeEntry("setsid", kGen, kGen, kGen, kGen, kGen, kGen),              // 66
    MakeEntry("sigaction", kHex, kHex, kHex, kHex, kHex, kHex),           // 67
    MakeEntry("sgetmask", kHex, kHex, kHex, kHex, kHex, kHex),            // 68
    MakeEntry("ssetmask", kHex, kHex, kHex, kHex, kHex, kHex),            // 69
    MakeEntry("setreuid", kGen, kGen, kGen, kGen, kGen, kGen),            // 70
    MakeEntry("setregid", kGen, kGen, kGen, kGen, kGen, kGen),            // 71
    MakeEntry("sigsuspend", kHex, kHex, kHex, kHex, kHex, kHex),          // 72
    MakeEntry("sigpending", kHex, kHex, kHex, kHex, kHex, kHex),          // 73
    MakeEntry("sethostname", kGen, kGen, kGen, kGen, kGen, kGen),         // 74
    MakeEntry("setrlimit", kGen, kGen, kGen, kGen, kGen, kGen),           // 75
    MakeEntry("getrlimit", kGen, kGen, kGen, kGen, kGen, kGen),           // 76
    MakeEntry("getrusage", kGen, kGen, kGen, kGen, kGen, kGen),           // 77
    MakeEntry("gettimeofday", kHex, kHex, kGen, kGen, kGen, kGen),        // 78
    MakeEntry("settimeofday", kHex, kHex, kGen, kGen, kGen, kGen),        // 79
    MakeEntry("getgroups", kGen, kGen, kGen, kGen, kGen, kGen),           // 80
    MakeEntry("setgroups", kGen, kGen, kGen, kGen, kGen, kGen),           // 81
    MakeEntry("select", kGen, kGen, kGen, kGen, kGen, kGen),              // 82
    MakeEntry("symlink", kPath, kPath, kGen, kGen, kGen, kGen),           // 83
    MakeEntry("oldlstat", kHex, kHex, kHex, kHex, kHex, kHex),            // 84
    MakeEntry("readlink", kPath, kGen, kInt, kGen, kGen, kGen),           // 85
    MakeEntry("uselib", kPath, kGen, kGen, kGen, kGen, kGen),             // 86
    MakeEntry("swapon", kPath, kHex, kGen, kGen, kGen, kGen),             // 87
    MakeEntry("reboot", kGen, kGen, kGen, kGen, kGen, kGen),              // 88
    MakeEntry("readdir", kHex, kHex, kHex, kHex, kHex, kHex),             // 89
    MakeEntry("mmap", kHex, kInt, kHex, kHex, kInt, kInt),                // 90
    MakeEntry("munmap", kHex, kHex, kGen, kGen, kGen, kGen),              // 91
    MakeEntry("truncate", kPath, kInt, kGen, kGen, kGen, kGen),           // 92
    MakeEntry("ftruncate", kGen, kGen, kGen, kGen, kGen, kGen),           // 93
    MakeEntry("fchmod", kGen, kGen, kGen, kGen, kGen, kGen),              // 94
    MakeEntry("fchown", kGen, kGen, kGen, kGen, kGen, kGen),              // 95
    MakeEntry("getpriority", kGen, kGen, kGen, kGen, kGen, kGen),         // 96
    MakeEntry("setpriority", kGen, kGen, kGen, kGen, kGen, kGen),         // 97
    MakeEntry("profil", kHex, kHex, kHex, kHex, kHex, kHex),              // 98
    MakeEntry("statfs", kPath, kGen, kGen, kGen, kGen, kGen),             // 99
    MakeEntry("fstatfs", kGen, kGen, kGen, kGen, kGen, kGen),             // 100
    MakeEntry("ioperm", kGen, kGen, kGen, kGen, kGen, kGen),              // 101
    MakeEntry("socketcall", kHex, kHex, kHex, kHex, kHex, kHex),          // 102
    MakeEntry("syslog", kGen, kGen, kGen, kGen, kGen, kGen),              // 103
    MakeEntry("setitimer", kGen, kGen, kGen, kGen, kGen, kGen),           // 104
    MakeEntry("getitimer", kGen, kGen, kGen, kGen, kGen, kGen),           // 105
    MakeEntry("stat", kPath, kGen, kGen, kGen, kGen, kGen),               // 106
    MakeEntry("lstat", kPath, kGen, kGen, kGen, kGen, kGen),              // 107
    MakeEntry("fstat", kInt, kHex, kGen, kGen, kGen, kGen),               // 108
    MakeEntry("olduname", kHex, kHex, kHex, kHex, kHex, kHex),            // 109
    MakeEntry("iopl", kGen, kGen, kGen, kGen, kGen, kGen),                // 110
    MakeEntry("vhangup", kGen, kGen, kGen, kGen, kGen, kGen),             // 111
    MakeEntry("idle", kHex, kHex, kHex, kHex, kHex, kHex),                // 112
    MakeEntry("vm86", kHex, kHex, kHex, kHex, kHex, kHex),                // 113
    MakeEntry("wait4", kInt, kHex, kHex, kHex, kGen, kGen),               // 114
    MakeEntry("swapoff", kPath, kGen, kGen, kGen, kGen, kGen),            // 115
    MakeEntry("sysinfo", kGen, kGen, kGen, kGen, kGen, kGen),             // 116
    MakeEntry("ipc", kHex, kHex, kHex, kHex, kHex, kHex),                 // 117
    MakeEntry("fsync", kGen, kGen, kGen, kGen, kGen, kGen),               // 118
    MakeEntry("sigreturn", kHex, kHex, kHex, kHex, kHex, kHex),           // 119
    MakeEntry("clone", kCloneFlag, kHex, kHex, kHex, kHex, kGen),         // 120
    MakeEntry("setdomainname", kGen, kGen, kGen, kGen, kGen, kGen),       // 121
    MakeEntry("uname", kGen, kGen, kGen, kGen, kGen, kGen),               // 122
    MakeEntry("modify_ldt", kGen, kGen, kGen, kGen, kGen, kGen),          // 123
    MakeEntry("adjtimex", kGen, kGen, kGen, kGen, kGen, kGen),            // 124
    MakeEntry("mprotect", kHex, kHex, kHex, kGen, kGen, kGen),            // 125
    MakeEntry("sigprocmask", kHex, kHex, kHex, kHex, kHex, kHex),         // 126
    MakeEntry("create_module", kGen, kGen, kGen, kGen, kGen, kGen),       // 127
    MakeEntry("init_module", kGen, kGen, kGen, kGen, kGen, kGen),         // 128
    MakeEntry("delete_module", kGen, kGen, kGen, kGen, kGen, kGen),       // 129
    MakeEntry("get_kernel_syms", kGen, kGen, kGen, kGen, kGen, kGen),     // 130
    MakeEntry("quotactl", kInt, kPath, kInt, kGen, kGen, kGen),           // 131
    MakeEntry("getpgid", kGen, kGen, kGen, kGen, kGen, kGen),             // 132
    MakeEntry("fchdir", kGen, kGen, kGen, kGen, kGen, kGen),              // 133
    MakeEntry("bdflush", kHex, kHex, kHex, kHex, kHex, kHex),             // 134
    MakeEntry("sysfs", kGen, kGen, kGen, kGen, kGen, kGen),               // 135
    MakeEntry("personality", kGen, kGen, kGen, kGen, kGen, kGen),         // 136
    MakeEntry("afs_syscall", kGen, kGen, kGen, kGen, kGen, kGen),         // 137
    MakeEntry("setfsuid", kGen, kGen, kGen, kGen, kGen, kGen),            // 138
    MakeEntry("setfsgid", kGen, kGen, kGen, kGen, kGen, kGen),            // 139
    MakeEntry("_llseek", kHex, kHex, kHex, kHex, kHex, kHex),             // 140
    MakeEntry("getdents", kGen, kGen, kGen, kGen, kGen, kGen),            // 141
    MakeEntry("_newselect", kHex, kHex, kHex, kHex, kHex, kHex),          // 142
    MakeEntry("flock", kGen, kGen, kGen, kGen, kGen, kGen),               // 143
    MakeEntry("msync", kGen, kGen, kGen, kGen, kGen, kGen),               // 144
    MakeEntry("readv", kGen, kGen, kGen, kGen, kGen, kGen),               // 145
    MakeEntry("writev", kGen, kGen, kGen, kGen, kGen, kGen),              // 146
    MakeEntry("getsid", kGen, kGen, kGen, kGen, kGen, kGen),              // 147
    MakeEntry("fdatasync", kGen, kGen, kGen, kGen, kGen, kGen),           // 148
    MakeEntry("_sysctl", kGen, kGen, kGen, kGen, kGen, kGen),             // 149
    MakeEntry("mlock", kGen, kGen, kGen, kGen, kGen, kGen),               // 150
    MakeEntry("munlock", kGen, kGen, kGen, kGen, kGen, kGen),             // 151
    MakeEntry("mlockall", kGen, kGen, kGen, kGen, kGen, kGen),            // 152
    MakeEntry("munlockall", kGen, kGen, kGen, kGen, kGen, kGen),          // 153
    MakeEntry("sched_setparam", kGen, kGen, kGen, kGen, kGen, kGen),      // 154
    MakeEntry("sched_getparam", kGen, kGen, kGen, kGen, kGen, kGen),      // 155
    MakeEntry("sched_setscheduler", kGen, kGen, kGen, kGen, kGen, kGen),  // 156
    MakeEntry("sched_getscheduler", kGen, kGen, kGen, kGen, kGen, kGen),  // 157
    MakeEntry("sched_yield", kGen, kGen, kGen, kGen, kGen, kGen),         // 158
    MakeEntry("sched_get_priority_max", kGen, kGen, kGen, kGen, kGen,
              kGen),  // 159
    MakeEntry("sched_get_priority_min", kGen, kGen, kGen, kGen, kGen,
              kGen),  // 160
    MakeEntry("sched_rr_get_interval", kGen, kGen, kGen, kGen, kGen,
              kGen),                                                     // 161
    MakeEntry("nanosleep", kHex, kHex, kGen, kGen, kGen, kGen),          // 162
    MakeEntry("mremap", kGen, kGen, kGen, kGen, kGen, kGen),             // 163
    MakeEntry("setresuid", kGen, kGen, kGen, kGen, kGen, kGen),          // 164
    MakeEntry("getresuid", kGen, kGen, kGen, kGen, kGen, kGen),          // 165
    MakeEntry("query_module", kGen, kGen, kGen, kGen, kGen, kGen),       // 166
    MakeEntry("poll", kGen, kGen, kGen, kGen, kGen, kGen),               // 167
    MakeEntry("nfsservctl", kGen, kGen, kGen, kGen, kGen, kGen),         // 168
    MakeEntry("setresgid", kGen, kGen, kGen, kGen, kGen, kGen),          // 169
    MakeEntry("getresgid", kGen, kGen, kGen, kGen, kGen, kGen),          // 170
    MakeEntry("prctl", kInt, kHex, kHex, kHex, kHex, kGen),              // 171
    MakeEntry("rt_sigreturn", kGen, kGen, kGen, kGen, kGen, kGen),       // 172
    MakeEntry("rt_sigaction", kSignal, kHex, kHex, kInt, kGen, kGen),    // 173
    MakeEntry("rt_sigprocmask", kGen, kGen, kGen, kGen, kGen, kGen),     // 174
    MakeEntry("rt_sigpending", kGen, kGen, kGen, kGen, kGen, kGen),      // 175
    MakeEntry("rt_sigtimedwait", kGen, kGen, kGen, kGen, kGen, kGen),    // 176
    MakeEntry("rt_sigqueueinfo", kGen, kGen, kGen, kGen, kGen, kGen),    // 177
    MakeEntry("rt_sigsuspend", kGen, kGen, kGen, kGen, kGen, kGen),      // 178
    MakeEntry("pread64", kGen, kGen, kGen, kGen, kGen, kGen),            // 179
    MakeEntry("pwrite64", kGen, kGen, kGen, kGen, kGen, kGen),           // 180
    MakeEntry("chown", kPath, kInt, kInt, kGen, kGen, kGen),             // 181
    MakeEntry("getcwd", kGen, kGen, kGen, kGen, kGen, kGen),             // 182
    MakeEntry("capget", kGen, kGen, kGen, kGen, kGen, kGen),             // 183
    MakeEntry("capset", kGen, kGen, kGen, kGen, kGen, kGen),             // 184
    MakeEntry("sigaltstack", kGen, kGen, kGen, kGen, kGen, kGen),        // 185
    MakeEntry("sendfile", kGen, kGen, kGen, kGen, kGen, kGen),           // 186
    MakeEntry("getpmsg", kGen, kGen, kGen, kGen, kGen, kGen),            // 187
    MakeEntry("putpmsg", kGen, kGen, kGen, kGen, kGen, kGen),            // 188
    MakeEntry("vfork", kGen, kGen, kGen, kGen, kGen, kGen),              // 189
    MakeEntry("ugetrlimit", kHex, kHex, kHex, kHex, kHex, kHex),         // 190
    MakeEntry("readahead", kGen, kGen, kGen, kGen, kGen, kGen),          // 191
    MakeEntry("mmap2", kHex, kHex, kHex, kHex, kHex, kHex),              // 192
    MakeEntry("truncate64", kHex, kHex, kHex, kHex, kHex, kHex),         // 193
    MakeEntry("ftruncate64", kHex, kHex, kHex, kHex, kHex, kHex),        // 194
    MakeEntry("stat64", kHex, kHex, kHex, kHex, kHex, kHex),             // 195
    MakeEntry("lstat64", kHex, kHex, kHex, kHex, kHex, kHex),            // 196
    MakeEntry("fstat64", kHex, kHex, kHex, kHex, kHex, kHex),            // 197
    MakeEntry("pciconfig_read", kHex, kHex, kHex, kHex, kHex, kHex),     // 198
    MakeEntry("pciconfig_write", kHex, kHex, kHex, kHex, kHex, kHex),    // 199
    MakeEntry("pciconfig_iobase", kHex, kHex, kHex, kHex, kHex, kHex),   // 200
    MakeEntry("multiplexer", kHex, kHex, kHex, kHex, kHex, kHex),        // 201
    MakeEntry("getdents64", kGen, kGen, kGen, kGen, kGen, kGen),         // 202
    MakeEntry("pivot_root", kPath, kPath, kGen, kGen, kGen, kGen),       // 203
    MakeEntry("fcntl64", kHex, kHex, kHex, kHex, kHex, kHex),            // 204
    MakeEntry("madvise", kGen, kGen, kGen, kGen, kGen, kGen),            // 205
    MakeEntry("mincore", kGen, kGen, kGen, kGen, kGen, kGen),            // 206
    MakeEntry("gettid", kGen, kGen, kGen, kGen, kGen, kGen),             // 207
    MakeEntry("tkill", kInt, kSignal, kGen, kGen, kGen, kGen),           // 208
    MakeEntry("setxattr", kPath, kString, kGen, kInt, kHex, kGen),       // 209
    MakeEntry("lsetxattr", kPath, kString, kGen, kInt, kHex, kGen),      // 210
    MakeEntry("fsetxattr", kGen, kGen, kGen, kGen, kGen, kGen),          // 211
    MakeEntry("getxattr", kPath, kString, kGen, kInt, kGen, kGen),       // 212
    MakeEntry("lgetxattr", kPath, kString, kGen, kInt, kGen, kGen),      // 213
    MakeEntry("fgetxattr", kGen, kGen, kGen, kGen, kGen, kGen),          // 214
    MakeEntry("listxattr", kPath, kGen, kInt, kGen, kGen, kGen),         // 215
    MakeEntry("llistxattr", kPath, kGen, kInt, kGen, kGen, kGen),        // 216
    MakeEntry("flistxattr", kGen, kGen, kGen, kGen, kGen, kGen),         // 217
    MakeEntry("removexattr", kPath, kString, kGen, kGen, kGen, kGen),    // 218
    MakeEntry("lremovexattr", kGen, kGen, kGen, kGen, kGen, kGen),       // 219
    MakeEntry("fremovexattr", kGen, kGen, kGen, kGen, kGen, kGen),       // 220
    MakeEntry("futex", kGen, kGen, kGen, kGen, kGen, kGen),              // 221
    MakeEntry("sched_setaffinity", kGen, kGen, kGen, kGen, kGen, kGen),  // 222
    MakeEntry("sched_getaffinity", kGen, kGen, kGen, kGen, kGen, kGen),  // 223
    SYSCALLS_UNUSED("UNUSED224"),                                        // 224
    MakeEntry("tuxcall", kGen, kGen, kGen, kGen, kGen, kGen),            // 225
    MakeEntry("sendfile64", kHex, kHex, kHex, kHex, kHex, kHex),         // 226
    MakeEntry("io_setup", kGen, kGen, kGen, kGen, kGen, kGen),           // 227
    MakeEntry("io_destroy", kGen, kGen, kGen, kGen, kGen, kGen),         // 228
    MakeEntry("io_getevents", kGen, kGen, kGen, kGen, kGen, kGen),       // 229
    MakeEntry("io_submit", kGen, kGen, kGen, kGen, kGen, kGen),          // 230
    MakeEntry("io_cancel", kGen, kGen, kGen, kGen, kGen, kGen),          // 231
    MakeEntry("set_tid_address", kHex, kGen, kGen, kGen, kGen, kGen),    // 232
    MakeEntry("fadvise64", kGen, kGen, kGen, kGen, kGen, kGen),          // 233
    MakeEntry("exit_group", kInt, kGen, kGen, kGen, kGen, kGen),         // 234
    MakeEntry("lookup_dcookie", kGen, kGen, kGen, kGen, kGen, kGen),     // 235
    MakeEntry("epoll_create", kGen, kGen, kGen, kGen, kGen, kGen),       // 236
    MakeEntry("epoll_ctl", kGen, kGen, kGen, kGen, kGen, kGen),          // 237
    MakeEntry("epoll_wait", kGen, kGen, kGen, kGen, kGen, kGen),         // 238
    MakeEntry("remap_file_pages", kGen, kGen, kGen, kGen, kGen, kGen),   // 239
    MakeEntry("timer_create", kGen, kGen, kGen, kGen, kGen, kGen),       // 240
    MakeEntry("timer_settime", kGen, kGen, kGen, kGen, kGen, kGen),      // 241
    MakeEntry("timer_gettime", kGen, kGen, kGen, kGen, kGen, kGen),      // 242
    MakeEntry("timer_getoverrun", kGen, kGen, kGen, kGen, kGen, kGen),   // 243
    MakeEntry("timer_delete", kGen, kGen, kGen, kGen, kGen, kGen),       // 244
    MakeEntry("clock_settime", kGen, kGen, kGen, kGen, kGen, kGen),      // 245
    MakeEntry("clock_gettime", kGen, kGen, kGen, kGen, kGen, kGen),      // 246
    MakeEntry("clock_getres", kGen, kGen, kGen, kGen, kGen, kGen),       // 247
    MakeEntry("clock_nanosleep", kGen, kGen, kGen, kGen, kGen, kGen),    // 248
    MakeEntry("swapcontext", kHex, kHex, kHex, kHex, kHex, kHex),        // 249
    MakeEntry("tgkill", kInt, kInt, kSignal, kGen, kGen, kGen),          // 250
    MakeEntry("utimes", kGen, kGen, kGen, kGen, kGen, kGen),             // 251
    MakeEntry("statfs64", kHex, kHex, kHex, kHex, kHex, kHex),           // 252
    MakeEntry("fstatfs64", kHex, kHex, kHex, kHex, kHex, kHex),          // 253
    MakeEntry("fadvise64_64", kHex, kHex, kHex, kHex, kHex, kHex),       // 254
    MakeEntry("rtas", kHex, kHex, kHex, kHex, kHex, kHex),               // 255
    MakeEntry("sys_debug_setcontext", kHex, kHex, kHex, kHex, kHex,
              kHex),                                                     // 256
    SYSCALLS_UNUSED("UNUSED257"),                                        // 257
    MakeEntry("migrate_pages", kGen, kGen, kGen, kGen, kGen, kGen),      // 258
    MakeEntry("mbind", kGen, kGen, kGen, kGen, kGen, kGen),              // 259
    MakeEntry("get_mempolicy", kGen, kGen, kGen, kGen, kGen, kGen),      // 260
    MakeEntry("set_mempolicy", kGen, kGen, kGen, kGen, kGen, kGen),      // 261
    MakeEntry("mq_open", kGen, kGen, kGen, kGen, kGen, kGen),            // 262
    MakeEntry("mq_unlink", kGen, kGen, kGen, kGen, kGen, kGen),          // 263
    MakeEntry("mq_timedsend", kGen, kGen, kGen, kGen, kGen, kGen),       // 264
    MakeEntry("mq_timedreceive", kGen, kGen, kGen, kGen, kGen, kGen),    // 265
    MakeEntry("mq_notify", kGen, kGen, kGen, kGen, kGen, kGen),          // 266
    MakeEntry("mq_getsetattr", kGen, kGen, kGen, kGen, kGen, kGen),      // 267
    MakeEntry("kexec_load", kGen, kGen, kGen, kGen, kGen, kGen),         // 268
    MakeEntry("add_key", kGen, kGen, kGen, kGen, kGen, kGen),            // 269
    MakeEntry("request_key", kGen, kGen, kGen, kGen, kGen, kGen),        // 270
    MakeEntry("keyctl", kGen, kGen, kGen, kGen, kGen, kGen),             // 271
    MakeEntry("waitid", kGen, kGen, kGen, kGen, kGen, kGen),             // 272
    MakeEntry("ioprio_set", kGen, kGen, kGen, kGen, kGen, kGen),         // 273
    MakeEntry("ioprio_get", kGen, kGen, kGen, kGen, kGen, kGen),         // 274
    MakeEntry("inotify_init", kGen, kGen, kGen, kGen, kGen, kGen),       // 275
    MakeEntry("inotify_add_watch", kGen, kGen, kGen, kGen, kGen, kGen),  // 276
    MakeEntry("inotify_rm_watch", kGen, kGen, kGen, kGen, kGen, kGen),   // 277
    MakeEntry("spu_run", kHex, kHex, kHex, kHex, kHex, kHex),            // 278
    MakeEntry("spu_create", kHex, kHex, kHex, kHex, kHex, kHex),         // 279
    MakeEntry("pselect6", kGen, kGen, kGen, kGen, kGen, kGen),           // 280
    MakeEntry("ppoll", kGen, kGen, kGen, kGen, kGen, kGen),              // 281
    MakeEntry("unshare", kGen, kGen, kGen, kGen, kGen, kGen),            // 282
    MakeEntry("splice", kGen, kGen, kGen, kGen, kGen, kGen),             // 283
    MakeEntry("tee", kGen, kGen, kGen, kGen, kGen, kGen),                // 284
    MakeEntry("vmsplice", kGen, kGen, kGen, kGen, kGen, kGen),           // 285
    MakeEntry("openat", kGen, kPath, kOct, kHex, kGen, kGen),            // 286
    MakeEntry("mkdirat", kGen, kPath, kGen, kGen, kGen, kGen),           // 287
    MakeEntry("mknodat", kGen, kPath, kGen, kGen, kGen, kGen),           // 288
    MakeEntry("fchownat", kGen, kPath, kGen, kGen, kGen, kGen),          // 289
    MakeEntry("futimesat", kGen, kPath, kGen, kGen, kGen, kGen),         // 290
    MakeEntry("newfstatat", kGen, kPath, kGen, kGen, kGen, kGen),        // 291
    MakeEntry("unlinkat", kGen, kPath, kGen, kGen, kGen, kGen),          // 292
    MakeEntry("renameat", kGen, kPath, kGen, kPath, kGen, kGen),         // 293
    MakeEntry("linkat", kGen, kPath, kGen, kPath, kGen, kGen),           // 294
    MakeEntry("symlinkat", kPath, kGen, kPath, kGen, kGen, kGen),        // 295
    MakeEntry("readlinkat", kGen, kPath, kGen, kGen, kGen, kGen),        // 296
    MakeEntry("fchmodat", kGen, kPath, kGen, kGen, kGen, kGen),          // 297
    MakeEntry("faccessat", kGen, kPath, kGen, kGen, kGen, kGen),         // 298
    MakeEntry("get_robust_list", kGen, kGen, kGen, kGen, kGen, kGen),    // 299
    MakeEntry("set_robust_list", kGen, kGen, kGen, kGen, kGen, kGen),    // 300
    MakeEntry("move_pages", kGen, kGen, kGen, kGen, kGen, kGen),         // 301
    MakeEntry("getcpu", kHex, kHex, kHex, kGen, kGen, kGen),             // 302
    MakeEntry("epoll_pwait", kGen, kGen, kGen, kGen, kGen, kGen),        // 303
    MakeEntry("utimensat", kGen, kGen, kGen, kGen, kGen, kGen),          // 304
    MakeEntry("signalfd", kGen, kGen, kGen, kGen, kGen, kGen),           // 305
    MakeEntry("timerfd_create", kGen, kGen, kGen, kGen, kGen, kGen),     // 306
    MakeEntry("eventfd", kGen, kGen, kGen, kGen, kGen, kGen),            // 307
    MakeEntry("sync_file_range2", kHex, kHex, kHex, kHex, kHex, kHex),   // 308
    MakeEntry("fallocate", kGen, kGen, kGen, kGen, kGen, kGen),          // 309
    MakeEntry("subpage_prot", kHex, kHex, kHex, kHex, kHex, kHex),       // 310
    MakeEntry("timerfd_settime", kGen, kGen, kGen, kGen, kGen, kGen),    // 311
    MakeEntry("timerfd_gettime", kGen, kGen, kGen, kGen, kGen, kGen),    // 312
    MakeEntry("signalfd4", kGen, kGen, kGen, kGen, kGen, kGen),          // 313
    MakeEntry("eventfd2", kGen, kGen, kGen, kGen, kGen, kGen),           // 314
    MakeEntry("epoll_create1", kGen, kGen, kGen, kGen, kGen, kGen),      // 315
    MakeEntry("dup3", kGen, kGen, kGen, kGen, kGen, kGen),               // 316
    MakeEntry("pipe2", kGen, kGen, kGen, kGen, kGen, kGen),              // 317
    MakeEntry("inotify_init1", kGen, kGen, kGen, kGen, kGen, kGen),      // 318
    MakeEntry("perf_event_open", kGen, kGen, kGen, kGen, kGen, kGen),    // 319
    MakeEntry("preadv", kGen, kGen, kGen, kGen, kGen, kGen),             // 320
    MakeEntry("pwritev", kGen, kGen, kGen, kGen, kGen, kGen),            // 321
    MakeEntry("rt_tgsigqueueinfo", kGen, kGen, kGen, kGen, kGen, kGen),  // 322
    MakeEntry("fanotify_init", kHex, kHex, kInt, kGen, kGen, kGen),      // 323
    MakeEntry("fanotify_mark", kInt, kHex, kInt, kPath, kGen, kGen),     // 324
    MakeEntry("prlimit64", kInt, kInt, kHex, kHex, kGen, kGen),          // 325
    MakeEntry("socket", kAddressFamily, kInt, kInt, kGen, kGen, kGen),   // 326
    MakeEntry("bind", kGen, kGen, kGen, kGen, kGen, kGen),               // 327
    MakeEntry("connect", kInt, kSockaddr, kInt, kGen, kGen, kGen),       // 328
    MakeEntry("listen", kGen, kGen, kGen, kGen, kGen, kGen),             // 329
    MakeEntry("accept", kGen, kGen, kGen, kGen, kGen, kGen),             // 330
    MakeEntry("getsockname", kGen, kGen, kGen, kGen, kGen, kGen),        // 331
    MakeEntry("getpeername", kGen, kGen, kGen, kGen, kGen, kGen),        // 332
    MakeEntry("socketpair", kGen, kGen, kGen, kGen, kGen, kGen),         // 333
    MakeEntry("send", kHex, kHex, kHex, kHex, kHex, kHex),               // 334
    MakeEntry("sendto", kInt, kGen, kInt, kHex, kSockaddr, kInt),        // 335
    MakeEntry("recv", kHex, kHex, kHex, kHex, kHex, kHex),               // 336
    MakeEntry("recvfrom", kGen, kGen, kGen, kGen, kGen, kGen),           // 337
    MakeEntry("shutdown", kGen, kGen, kGen, kGen, kGen, kGen),           // 338
    MakeEntry("setsockopt", kGen, kGen, kGen, kGen, kGen, kGen),         // 339
    MakeEntry("getsockopt", kGen, kGen, kGen, kGen, kGen, kGen),         // 340
    MakeEntry("sendmsg", kInt, kSockmsghdr, kHex, kGen, kGen, kGen),     // 341
    MakeEntry("recvmsg", kGen, kGen, kGen, kGen, kGen, kGen),            // 342
    MakeEntry("recvmmsg", kInt, kHex, kHex, kHex, kGen, kGen),           // 343
    MakeEntry("accept4", kGen, kGen, kGen, kGen, kGen, kGen),            // 344
    MakeEntry("name_to_handle_at", kInt, kGen, kHex, kHex, kHex, kGen),  // 345
    MakeEntry("open_by_handle_at", kInt, kHex, kHex, kGen, kGen, kGen),  // 346
    MakeEntry("clock_adjtime", kInt, kHex, kGen, kGen, kGen, kGen),      // 347
    MakeEntry("syncfs", kInt, kGen, kGen, kGen, kGen, kGen),             // 348
    MakeEntry("sendmmsg", kInt, kHex, kInt, kHex, kGen, kGen),           // 349
    MakeEntry("setns", kInt, kHex, kGen, kGen, kGen, kGen),              // 350
    MakeEntry("process_vm_readv", kInt, kHex, kInt, kHex, kInt, kInt),   // 351
    MakeEntry("process_vm_writev", kInt, kHex, kInt, kHex, kInt, kInt),  // 352
    MakeEntry("finit_module", kInt, kPath, kHex, kGen, kGen, kGen),      // 353
    MakeEntry("kcmp", kInt, kInt, kInt, kHex, kHex, kGen),               // 354
    MakeEntry("sched_setattr", kGen, kGen, kGen, kGen, kGen, kGen),      // 355
    MakeEntry("sched_getattr", kGen, kGen, kGen, kGen, kGen, kGen),      // 356
    MakeEntry("renameat2", kGen, kPath, kGen, kPath, kGen, kGen),        // 357
    MakeEntry("seccomp", kGen, kGen, kGen, kGen, kGen, kGen),            // 358
    MakeEntry("getrandom", kGen, kGen, kGen, kGen, kGen, kGen),          // 359
    MakeEntry("memfd_create", kGen, kGen, kGen, kGen, kGen, kGen),       // 360
    MakeEntry("bpf", kHex, kHex, kHex, kHex, kHex, kHex),                // 361
    MakeEntry("execveat", kHex, kHex, kHex, kHex, kHex, kHex),           // 362
    MakeEntry("switch_endian", kHex, kHex, kHex, kHex, kHex, kHex),      // 363
    MakeEntry("userfaultfd", kHex, kHex, kHex, kHex, kHex, kHex),        // 364
    MakeEntry("membarrier", kHex, kHex, kHex, kHex, kHex, kHex),         // 365
    SYSCALLS_UNUSED("UNUSED366"),                                        // 366
    SYSCALLS_UNUSED("UNUSED367"),                                        // 367
    SYSCALLS_UNUSED("UNUSED368"),                                        // 368
    SYSCALLS_UNUSED("UNUSED369"),                                        // 369
    SYSCALLS_UNUSED("UNUSED370"),                                        // 370
    SYSCALLS_UNUSED("UNUSED371"),                                        // 371
    SYSCALLS_UNUSED("UNUSED372"),                                        // 372
    SYSCALLS_UNUSED("UNUSED373"),                                        // 373
    SYSCALLS_UNUSED("UNUSED374"),                                        // 374
    SYSCALLS_UNUSED("UNUSED375"),                                        // 375
    SYSCALLS_UNUSED("UNUSED376"),                                        // 376
    SYSCALLS_UNUSED("UNUSED377"),                                        // 377
    MakeEntry("mlock2", kHex, kHex, kHex, kHex, kHex, kHex),             // 378
    MakeEntry("copy_file_range", kHex, kHex, kHex, kHex, kHex, kHex),    // 379
    MakeEntry("preadv2", kHex, kHex, kHex, kHex, kHex, kHex),            // 380
    MakeEntry("pwritev2", kHex, kHex, kHex, kHex, kHex, kHex),           // 381
};

// TODO(cblichmann): Confirm the entries in this list.
// https://github.com/torvalds/linux/blob/v5.8/include/uapi/asm-generic/unistd.h
constexpr std::array kSyscallDataArm64 = {
    MakeEntry("io_setup", UnknownArguments()),                           // 0
    MakeEntry("io_destroy", UnknownArguments()),                         // 1
    MakeEntry("io_submit", UnknownArguments()),                          // 2
    MakeEntry("io_cancel", UnknownArguments()),                          // 3
    MakeEntry("io_getevents", UnknownArguments()),                       // 4
    MakeEntry("setxattr", kPath, kString, kGen, kInt, kHex, kGen),       // 5
    MakeEntry("lsetxattr", kPath, kString, kGen, kInt, kHex, kGen),      // 6
    MakeEntry("fsetxattr", UnknownArguments()),                          // 7
    MakeEntry("getxattr", kPath, kString, kGen, kInt, kGen, kGen),       // 8
    MakeEntry("lgetxattr", kPath, kString, kGen, kInt, kGen, kGen),      // 9
    MakeEntry("fgetxattr", UnknownArguments()),                          // 10
    MakeEntry("listxattr", kPath, kGen, kInt, kGen, kGen, kGen),         // 11
    MakeEntry("llistxattr", kPath, kGen, kInt, kGen, kGen, kGen),        // 12
    MakeEntry("flistxattr", UnknownArguments()),                         // 13
    MakeEntry("removexattr", kPath, kString, kGen, kGen, kGen, kGen),    // 14
    MakeEntry("lremovexattr", UnknownArguments()),                       // 15
    MakeEntry("fremovexattr", UnknownArguments()),                       // 16
    MakeEntry("getcwd", UnknownArguments()),                             // 17
    MakeEntry("lookup_dcookie", UnknownArguments()),                     // 18
    MakeEntry("eventfd2", UnknownArguments()),                           // 19
    MakeEntry("epoll_create1", UnknownArguments()),                      // 20
    MakeEntry("epoll_ctl", UnknownArguments()),                          // 21
    MakeEntry("epoll_pwait", UnknownArguments()),                        // 22
    MakeEntry("dup", UnknownArguments()),                                // 23
    MakeEntry("dup3", UnknownArguments()),                               // 24
    MakeEntry("fcntl", UnknownArguments()),                              // 25
    MakeEntry("inotify_init1", UnknownArguments()),                      // 26
    MakeEntry("inotify_add_watch", UnknownArguments()),                  // 27
    MakeEntry("inotify_rm_watch", UnknownArguments()),                   // 28
    MakeEntry("ioctl", UnknownArguments()),                              // 29
    MakeEntry("ioprio_set", UnknownArguments()),                         // 30
    MakeEntry("ioprio_get", UnknownArguments()),                         // 31
    MakeEntry("flock", UnknownArguments()),                              // 32
    MakeEntry("mknodat", kGen, kPath, kGen, kGen, kGen, kGen),           // 33
    MakeEntry("mkdirat", kGen, kPath, kGen, kGen, kGen, kGen),           // 34
    MakeEntry("unlinkat", kGen, kPath, kGen, kGen, kGen, kGen),          // 35
    MakeEntry("symlinkat", kPath, kGen, kPath, kGen, kGen, kGen),        // 36
    MakeEntry("linkat", kGen, kPath, kGen, kPath, kGen, kGen),           // 37
    MakeEntry("renameat", kGen, kPath, kGen, kPath, kGen, kGen),         // 38
    MakeEntry("umount2", kPath, kHex, kGen, kGen, kGen, kGen),           // 39
    MakeEntry("mount", kPath, kPath, kString, kHex, kGen, kGen),         // 40
    MakeEntry("pivot_root", kPath, kPath, kGen, kGen, kGen, kGen),       // 41
    MakeEntry("nfsservctl", UnknownArguments()),                         // 42
    MakeEntry("statfs", kPath, kGen, kGen, kGen, kGen, kGen),            // 43
    MakeEntry("fstatfs", UnknownArguments()),                            // 44
    MakeEntry("truncate", kPath, kInt, kGen, kGen, kGen, kGen),          // 45
    MakeEntry("ftruncate", UnknownArguments()),                          // 46
    MakeEntry("fallocate", UnknownArguments()),                          // 47
    MakeEntry("faccessat", kGen, kPath, kGen, kGen, kGen, kGen),         // 48
    MakeEntry("chdir", kPath, kGen, kGen, kGen, kGen, kGen),             // 49
    MakeEntry("fchdir", UnknownArguments()),                             // 50
    MakeEntry("chroot", kPath, kGen, kGen, kGen, kGen, kGen),            // 51
    MakeEntry("fchmod", UnknownArguments()),                             // 52
    MakeEntry("fchmodat", kGen, kPath, kGen, kGen, kGen, kGen),          // 53
    MakeEntry("fchownat", kGen, kPath, kGen, kGen, kGen, kGen),          // 54
    MakeEntry("fchown", UnknownArguments()),                             // 55
    MakeEntry("openat", kGen, kPath, kOct, kHex, kGen, kGen),            // 56
    MakeEntry("close", kInt, kGen, kGen, kGen, kGen, kGen),              // 57
    MakeEntry("vhangup", UnknownArguments()),                            // 58
    MakeEntry("pipe2", UnknownArguments()),                              // 59
    MakeEntry("quotactl", kInt, kPath, kInt, kGen, kGen, kGen),          // 60
    MakeEntry("getdents64", UnknownArguments()),                         // 61
    MakeEntry("lseek", UnknownArguments()),                              // 62
    MakeEntry("read", kInt, kHex, kInt, kGen, kGen, kGen),               // 63
    MakeEntry("write", kInt, kHex, kInt, kGen, kGen, kGen),              // 64
    MakeEntry("readv", UnknownArguments()),                              // 65
    MakeEntry("writev", UnknownArguments()),                             // 66
    MakeEntry("pread64", UnknownArguments()),                            // 67
    MakeEntry("pwrite64", UnknownArguments()),                           // 68
    MakeEntry("preadv", UnknownArguments()),                             // 69
    MakeEntry("pwritev", UnknownArguments()),                            // 70
    MakeEntry("sendfile", UnknownArguments()),                           // 71
    MakeEntry("pselect6", UnknownArguments()),                           // 72
    MakeEntry("ppoll", UnknownArguments()),                              // 73
    MakeEntry("signalfd4", UnknownArguments()),                          // 74
    MakeEntry("vmsplice", UnknownArguments()),                           // 75
    MakeEntry("splice", UnknownArguments()),                             // 76
    MakeEntry("tee", UnknownArguments()),                                // 77
    MakeEntry("readlinkat", kGen, kPath, kGen, kGen, kGen, kGen),        // 78
    MakeEntry("newfstatat", kGen, kPath, kGen, kGen, kGen, kGen),        // 79
    MakeEntry("fstat", kInt, kHex, kGen, kGen, kGen, kGen),              // 80
    MakeEntry("sync", UnknownArguments()),                               // 81
    MakeEntry("fsync", UnknownArguments()),                              // 82
    MakeEntry("fdatasync", UnknownArguments()),                          // 83
    MakeEntry("sync_file_range", UnknownArguments()),                    // 84
    MakeEntry("timerfd_create", UnknownArguments()),                     // 85
    MakeEntry("timerfd_settime", UnknownArguments()),                    // 86
    MakeEntry("timerfd_gettime", UnknownArguments()),                    // 87
    MakeEntry("utimensat", UnknownArguments()),                          // 88
    MakeEntry("acct", kPath, kGen, kGen, kGen, kGen, kGen),              // 89
    MakeEntry("capget", UnknownArguments()),                             // 90
    MakeEntry("capset", UnknownArguments()),                             // 91
    MakeEntry("personality", UnknownArguments()),                        // 92
    MakeEntry("exit", kInt, kGen, kGen, kGen, kGen, kGen),               // 93
    MakeEntry("exit_group", kInt, kGen, kGen, kGen, kGen, kGen),         // 94
    MakeEntry("waitid", UnknownArguments()),                             // 95
    MakeEntry("set_tid_address", kHex, kGen, kGen, kGen, kGen, kGen),    // 96
    MakeEntry("unshare", UnknownArguments()),                            // 97
    MakeEntry("futex", UnknownArguments()),                              // 98
    MakeEntry("set_robust_list", UnknownArguments()),                    // 99
    MakeEntry("get_robust_list", UnknownArguments()),                    // 100
    MakeEntry("nanosleep", kHex, kHex, kGen, kGen, kGen, kGen),          // 101
    MakeEntry("getitimer", UnknownArguments()),                          // 102
    MakeEntry("setitimer", UnknownArguments()),                          // 103
    MakeEntry("kexec_load", UnknownArguments()),                         // 104
    MakeEntry("init_module", UnknownArguments()),                        // 105
    MakeEntry("delete_module", UnknownArguments()),                      // 106
    MakeEntry("timer_create", UnknownArguments()),                       // 107
    MakeEntry("timer_gettime", UnknownArguments()),                      // 108
    MakeEntry("timer_getoverrun", UnknownArguments()),                   // 109
    MakeEntry("timer_settime", UnknownArguments()),                      // 110
    MakeEntry("timer_delete", UnknownArguments()),                       // 111
    MakeEntry("clock_settime", UnknownArguments()),                      // 112
    MakeEntry("clock_gettime", UnknownArguments()),                      // 113
    MakeEntry("clock_getres", UnknownArguments()),                       // 114
    MakeEntry("clock_nanosleep", UnknownArguments()),                    // 115
    MakeEntry("syslog", UnknownArguments()),                             // 116
    MakeEntry("ptrace", UnknownArguments()),                             // 117
    MakeEntry("sched_setparam", UnknownArguments()),                     // 118
    MakeEntry("sched_setscheduler", UnknownArguments()),                 // 119
    MakeEntry("sched_getscheduler", UnknownArguments()),                 // 120
    MakeEntry("sched_getparam", UnknownArguments()),                     // 121
    MakeEntry("sched_setaffinity", UnknownArguments()),                  // 122
    MakeEntry("sched_getaffinity", UnknownArguments()),                  // 123
    MakeEntry("sched_yield", UnknownArguments()),                        // 124
    MakeEntry("sched_get_priority_max", UnknownArguments()),             // 125
    MakeEntry("sched_get_priority_min", UnknownArguments()),             // 126
    MakeEntry("sched_rr_get_interval", UnknownArguments()),              // 127
    MakeEntry("restart_syscall", UnknownArguments()),                    // 128
    MakeEntry("kill", kInt, kSignal, kGen, kGen, kGen, kGen),            // 129
    MakeEntry("tkill", kInt, kSignal, kGen, kGen, kGen, kGen),           // 130
    MakeEntry("tgkill", kInt, kInt, kSignal, kGen, kGen, kGen),          // 131
    MakeEntry("sigaltstack", UnknownArguments()),                        // 132
    MakeEntry("rt_sigsuspend", UnknownArguments()),                      // 133
    MakeEntry("rt_sigaction", kSignal, kHex, kHex, kInt, kGen, kGen),    // 134
    MakeEntry("rt_sigprocmask", UnknownArguments()),                     // 135
    MakeEntry("rt_sigpending", UnknownArguments()),                      // 136
    MakeEntry("rt_sigtimedwait", UnknownArguments()),                    // 137
    MakeEntry("rt_sigqueueinfo", UnknownArguments()),                    // 138
    MakeEntry("rt_sigreturn", UnknownArguments()),                       // 139
    MakeEntry("setpriority", UnknownArguments()),                        // 140
    MakeEntry("getpriority", UnknownArguments()),                        // 141
    MakeEntry("reboot", UnknownArguments()),                             // 142
    MakeEntry("setregid", UnknownArguments()),                           // 143
    MakeEntry("setgid", UnknownArguments()),                             // 144
    MakeEntry("setreuid", UnknownArguments()),                           // 145
    MakeEntry("setuid", UnknownArguments()),                             // 146
    MakeEntry("setresuid", UnknownArguments()),                          // 147
    MakeEntry("getresuid", UnknownArguments()),                          // 148
    MakeEntry("setresgid", UnknownArguments()),                          // 149
    MakeEntry("getresgid", UnknownArguments()),                          // 150
    MakeEntry("setfsuid", UnknownArguments()),                           // 151
    MakeEntry("setfsgid", UnknownArguments()),                           // 152
    MakeEntry("times", UnknownArguments()),                              // 153
    MakeEntry("setpgid", UnknownArguments()),                            // 154
    MakeEntry("getpgid", UnknownArguments()),                            // 155
    MakeEntry("getsid", UnknownArguments()),                             // 156
    MakeEntry("setsid", UnknownArguments()),                             // 157
    MakeEntry("getgroups", UnknownArguments()),                          // 158
    MakeEntry("setgroups", UnknownArguments()),                          // 159
    MakeEntry("uname", UnknownArguments()),                              // 160
    MakeEntry("sethostname", UnknownArguments()),                        // 161
    MakeEntry("setdomainname", UnknownArguments()),                      // 162
    MakeEntry("getrlimit", UnknownArguments()),                          // 163
    MakeEntry("setrlimit", UnknownArguments()),                          // 164
    MakeEntry("getrusage", UnknownArguments()),                          // 165
    MakeEntry("umask", kHex, kGen, kGen, kGen, kGen, kGen),              // 166
    MakeEntry("prctl", kInt, kHex, kHex, kHex, kHex, kGen),              // 167
    MakeEntry("getcpu", kHex, kHex, kHex, kGen, kGen, kGen),             // 168
    MakeEntry("gettimeofday", kHex, kHex, kGen, kGen, kGen, kGen),       // 169
    MakeEntry("settimeofday", kHex, kHex, kGen, kGen, kGen, kGen),       // 170
    MakeEntry("adjtimex", UnknownArguments()),                           // 171
    MakeEntry("getpid", UnknownArguments()),                             // 172
    MakeEntry("getppid", UnknownArguments()),                            // 173
    MakeEntry("getuid", UnknownArguments()),                             // 174
    MakeEntry("geteuid", UnknownArguments()),                            // 175
    MakeEntry("getgid", UnknownArguments()),                             // 176
    MakeEntry("getegid", UnknownArguments()),                            // 177
    MakeEntry("gettid", UnknownArguments()),                             // 178
    MakeEntry("sysinfo", UnknownArguments()),                            // 179
    MakeEntry("mq_open", UnknownArguments()),                            // 180
    MakeEntry("mq_unlink", UnknownArguments()),                          // 181
    MakeEntry("mq_timedsend", UnknownArguments()),                       // 182
    MakeEntry("mq_timedreceive", UnknownArguments()),                    // 183
    MakeEntry("mq_notify", UnknownArguments()),                          // 184
    MakeEntry("mq_getsetattr", UnknownArguments()),                      // 185
    MakeEntry("msgget", UnknownArguments()),                             // 186
    MakeEntry("msgctl", UnknownArguments()),                             // 187
    MakeEntry("msgrcv", UnknownArguments()),                             // 188
    MakeEntry("msgsnd", UnknownArguments()),                             // 189
    MakeEntry("semget", UnknownArguments()),                             // 190
    MakeEntry("semctl", UnknownArguments()),                             // 191
    MakeEntry("semtimedop", UnknownArguments()),                         // 192
    MakeEntry("semop", UnknownArguments()),                              // 193
    MakeEntry("shmget", UnknownArguments()),                             // 194
    MakeEntry("shmctl", UnknownArguments()),                             // 195
    MakeEntry("shmat", UnknownArguments()),                              // 196
    MakeEntry("shmdt", UnknownArguments()),                              // 197
    MakeEntry("socket", kAddressFamily, kInt, kInt, kGen, kGen, kGen),   // 198
    MakeEntry("socketpair", UnknownArguments()),                         // 199
    MakeEntry("bind", UnknownArguments()),                               // 200
    MakeEntry("listen", UnknownArguments()),                             // 201
    MakeEntry("accept", UnknownArguments()),                             // 202
    MakeEntry("connect", kInt, kSockaddr, kInt, kGen, kGen, kGen),       // 203
    MakeEntry("getsockname", UnknownArguments()),                        // 204
    MakeEntry("getpeername", UnknownArguments()),                        // 205
    MakeEntry("sendto", kInt, kGen, kInt, kHex, kSockaddr, kInt),        // 206
    MakeEntry("recvfrom", UnknownArguments()),                           // 207
    MakeEntry("setsockopt", UnknownArguments()),                         // 208
    MakeEntry("getsockopt", UnknownArguments()),                         // 209
    MakeEntry("shutdown", UnknownArguments()),                           // 210
    MakeEntry("sendmsg", kInt, kSockmsghdr, kHex, kGen, kGen, kGen),     // 211
    MakeEntry("recvmsg", UnknownArguments()),                            // 212
    MakeEntry("readahead", UnknownArguments()),                          // 213
    MakeEntry("brk", kHex, kGen, kGen, kGen, kGen, kGen),                // 214
    MakeEntry("munmap", kHex, kHex, kGen, kGen, kGen, kGen),             // 215
    MakeEntry("mremap", UnknownArguments()),                             // 216
    MakeEntry("add_key", UnknownArguments()),                            // 217
    MakeEntry("request_key", UnknownArguments()),                        // 218
    MakeEntry("keyctl", UnknownArguments()),                             // 219
    MakeEntry("clone", kCloneFlag, kHex, kHex, kHex, kHex, kGen),        // 220
    MakeEntry("execve", kPath, kHex, kHex, kGen, kGen, kGen),            // 221
    MakeEntry("mmap", kHex, kInt, kHex, kHex, kInt, kInt),               // 222
    MakeEntry("fadvise64", UnknownArguments()),                          // 223
    MakeEntry("swapon", kPath, kHex, kGen, kGen, kGen, kGen),            // 224
    MakeEntry("swapoff", kPath, kGen, kGen, kGen, kGen, kGen),           // 225
    MakeEntry("mprotect", kHex, kHex, kHex, kGen, kGen, kGen),           // 226
    MakeEntry("msync", UnknownArguments()),                              // 227
    MakeEntry("mlock", UnknownArguments()),                              // 228
    MakeEntry("munlock", UnknownArguments()),                            // 229
    MakeEntry("mlockall", UnknownArguments()),                           // 230
    MakeEntry("munlockall", UnknownArguments()),                         // 231
    MakeEntry("mincore", UnknownArguments()),                            // 232
    MakeEntry("madvise", UnknownArguments()),                            // 233
    MakeEntry("remap_file_pages", UnknownArguments()),                   // 234
    MakeEntry("mbind", UnknownArguments()),                              // 235
    MakeEntry("get_mempolicy", UnknownArguments()),                      // 236
    MakeEntry("set_mempolicy", UnknownArguments()),                      // 237
    MakeEntry("migrate_pages", UnknownArguments()),                      // 238
    MakeEntry("move_pages", UnknownArguments()),                         // 239
    MakeEntry("rt_tgsigqueueinfo", UnknownArguments()),                  // 240
    MakeEntry("perf_event_open", UnknownArguments()),                    // 241
    MakeEntry("accept4", UnknownArguments()),                            // 242
    MakeEntry("recvmmsg", kInt, kHex, kHex, kHex, kGen, kGen),           // 243
    SYSCALLS_UNUSED("UNUSED244"),                                        // 244
    SYSCALLS_UNUSED("UNUSED245"),                                        // 245
    SYSCALLS_UNUSED("UNUSED246"),                                        // 246
    SYSCALLS_UNUSED("UNUSED247"),                                        // 247
    SYSCALLS_UNUSED("UNUSED248"),                                        // 248
    SYSCALLS_UNUSED("UNUSED249"),                                        // 249
    SYSCALLS_UNUSED("UNUSED250"),                                        // 250
    SYSCALLS_UNUSED("UNUSED251"),                                        // 251
    SYSCALLS_UNUSED("UNUSED252"),                                        // 252
    SYSCALLS_UNUSED("UNUSED253"),                                        // 253
    SYSCALLS_UNUSED("UNUSED254"),                                        // 254
    SYSCALLS_UNUSED("UNUSED255"),                                        // 255
    SYSCALLS_UNUSED("UNUSED256"),                                        // 256
    SYSCALLS_UNUSED("UNUSED257"),                                        // 257
    SYSCALLS_UNUSED("UNUSED258"),                                        // 258
    SYSCALLS_UNUSED("UNUSED259"),                                        // 259
    MakeEntry("wait4", kInt, kHex, kHex, kHex, kGen, kGen),              // 260
    MakeEntry("prlimit64", kInt, kInt, kHex, kHex, kGen, kGen),          // 261
    MakeEntry("fanotify_init", kHex, kHex, kInt, kGen, kGen, kGen),      // 262
    MakeEntry("fanotify_mark", kInt, kHex, kInt, kPath, kGen, kGen),     // 263
    MakeEntry("name_to_handle_at", kInt, kGen, kHex, kHex, kHex, kGen),  // 264
    MakeEntry("open_by_handle_at", kInt, kHex, kHex, kGen, kGen, kGen),  // 265
    MakeEntry("clock_adjtime", kInt, kHex, kGen, kGen, kGen, kGen),      // 266
    MakeEntry("syncfs", kInt, kGen, kGen, kGen, kGen, kGen),             // 267
    MakeEntry("setns", kInt, kHex, kGen, kGen, kGen, kGen),              // 268
    MakeEntry("sendmmsg", kInt, kHex, kInt, kHex, kGen, kGen),           // 269
    MakeEntry("process_vm_readv", kInt, kHex, kInt, kHex, kInt, kInt),   // 270
    MakeEntry("process_vm_writev", kInt, kHex, kInt, kHex, kInt, kInt),  // 271
    MakeEntry("kcmp", kInt, kInt, kInt, kHex, kHex, kGen),               // 272
    MakeEntry("finit_module", kInt, kPath, kHex, kGen, kGen, kGen),      // 273
    MakeEntry("sched_setattr", UnknownArguments()),                      // 274
    MakeEntry("sched_getattr", UnknownArguments()),                      // 275
    MakeEntry("renameat2", kGen, kPath, kGen, kPath, kGen, kGen),        // 276
    MakeEntry("seccomp", UnknownArguments()),                            // 277
    MakeEntry("getrandom", UnknownArguments()),                          // 278
    MakeEntry("memfd_create", UnknownArguments()),                       // 279
};

#undef SYSCALLS_UNUSED00_99
#undef SYSCALLS_UNUSED50_99
#undef SYSCALLS_UNUSED00_49
#undef SYSCALLS_UNUSED0_9
#undef SYSCALLS_UNUSED

SyscallTable SyscallTable::get(cpu::Architecture arch) {
  switch (arch) {
    case cpu::kX8664:
      return SyscallTable(kSyscallDataX8664);
    case cpu::kX86:
      return SyscallTable(kSyscallDataX8632);
    case cpu::kPPC64LE:
      return SyscallTable(kSyscallDataPPC64LE);
    case cpu::kArm64:
      return SyscallTable(kSyscallDataArm64);
    default:
      return SyscallTable();
  }
}

}  // namespace sandbox2
