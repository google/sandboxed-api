// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Checks various things related to namespaces, depending on the first argument:
// ./binary 0 <file1> <file2> ... <fileN>:
//    Make sure all provided files exist and are RO, return 0 on OK.
//    Returns the index of the first non-existing file on failure.
// ./binary 1 <file1> <file2> ... <fileN>:
//    Make sure all provided files exist and are RW, return 0 on OK.
//    Returns the index of the first non-existing file on failure.
// ./binary 2
//    Make sure that we run in a PID namespace (this implies getpid() == 1)
//    Returns 0 on OK.
// ./binary 3 <uid> <gid>
//    Make sure getuid()/getgid() returns the provided uid/gid (User namespace).
//    Returns 0 on OK.
// ./binary 4 <file1> <file2> ... <fileN>:
//    Create provided files, return 0 on OK.
//    Returns the index of the first non-creatable file on failure.
// ./binary 9 <file1> <file2> ... <fileN>:
//    Make sure all provided files exist and are opened using O_RDONLY.
//    This is needed to test Landlock, as `access()` doesn't trigger Landlock
//    restrictions. Returns 0 on OK.
// ./binary 10 <file1> <file2> ... <fileN>:
//    Reads the contents of the provided files, replacing null bytes with
//    spaces, and returns the content strings. Returns 0 on OK.
// ./binary 11:
//    Returns the list of all PIDs in /proc.
// ./binary 16 <unix_socket_path>:
//    Connects a SOCK_STREAM AF_UNIX socket to <unix_socket_path>.
// ./binary 17 <unix_socket_path>:
//    Sends a datagram via a SOCK_DGRAM AF_UNIX socket to <unix_socket_path>.
// ./binary 18 <exec_path>:
//    Forks and executes <exec_path>, reporting "exec_success" or
//    "exec_failed:<errno>".
// ./binary 19:
//    Tests socket() creation across AF_UNIX, AF_INET, AF_INET6, AF_NETLINK,
//    AF_PACKET, and AF_VSOCK domains.
// ./binary 20 <tcp_port> <udp_port>:
//    Tests TCP bind/connect and UDP bind/connect/sendto on 127.0.0.1.
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/netlink.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"

#ifndef AF_VSOCK
#define AF_VSOCK 40
#endif

namespace {

using sapi::file::JoinPath;
using sapi::file_util::fileops::ListDirectoryEntries;

bool IsDirectory(const std::string& path) {
  struct stat statbuf;
  PCHECK(lstat(path.c_str(), &statbuf) == 0) << "Failed to stat " << path;
  return statbuf.st_mode & S_IFDIR;
}

void ListDirectoriesRecursively(const std::string& path,
                                std::vector<std::string>& files) {
  std::string error;
  std::vector<std::string> entries;
  CHECK(ListDirectoryEntries(path, &entries, &error)) << error;
  for (const std::string& entry : entries) {
    std::string new_path = JoinPath(path, entry);
    // Don't descent into /sys or /proc, just mark their existence
    if (new_path == "/sys" || new_path == "/proc") {
      files.push_back(new_path);
      continue;
    }
    if (IsDirectory(new_path)) {
      ListDirectoriesRecursively(new_path, files);
    } else {
      files.push_back(new_path);
    }
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    return 0;
  }

  int mode = atoi(argv[1]);  // NOLINT(runtime/deprecated_fn)
  std::vector<std::string> result;

  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);

  switch (mode) {
    case 0:
      // Make sure file exist
      for (int i = 2; i < argc; i++) {
        if (access(argv[i], R_OK) == 0) {
          result.push_back(argv[i]);
        }
      }
      break;

    case 1:
      for (int i = 2; i < argc; i++) {
        if (access(argv[i], W_OK) == 0) {
          result.push_back(argv[i]);
        }
      }
      break;

    case 2:
      result.push_back(absl::StrCat(getpid()));
      break;

    case 3:
      result.push_back(absl::StrCat(getuid()));
      result.push_back(absl::StrCat(getgid()));
      break;

    case 4:
      for (int i = 2; i < argc; ++i) {
        if (open(argv[i], O_CREAT | O_WRONLY, 0644) != -1) {
          result.push_back(argv[i]);
        }
      }
      break;

    case 5: {
      absl::flat_hash_set<std::string> ifnames;
      struct ifaddrs* addrs;
      if (getifaddrs(&addrs)) {
        return -1;
      }
      for (struct ifaddrs* cur = addrs; cur; cur = cur->ifa_next) {
        ifnames.insert(cur->ifa_name);
      }
      result.insert(result.end(), ifnames.begin(), ifnames.end());
      freeifaddrs(addrs);
      break;
    }

    case 6:
      ListDirectoriesRecursively(argv[2], result);
      break;
    case 7: {
      char hostname[1000];
      if (gethostname(hostname, sizeof(hostname)) == -1) {
        return -1;
      }
      result.push_back(hostname);
      break;
    }
    case 8: {
      constexpr char kNsNetPath[] = "/proc/self/ns/net";
      std::string buf(100, '\0');
      if (readlink(kNsNetPath, buf.data(), buf.size()) == -1) {
        return -1;
      }
      result.push_back(buf);
      break;
    }
    case 9:
      // Landlock intercepts open(), but not access() or stat(). Therefore, mode
      // 0 (which uses access()) would incorrectly return success for a file
      // blocked by Landlock. Mode 9 explicitly calls open() to properly test
      // Landlock.
      for (int i = 2; i < argc; ++i) {
        int fd = open(argv[i], O_RDONLY);
        if (fd != -1) {
          result.push_back(argv[i]);
          close(fd);
        }
      }
      break;
    case 10: {
      for (int i = 2; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd != -1) {
          std::string content;
          char buf[4096];
          ssize_t n;
          while ((n = read(fd, buf, sizeof(buf))) > 0) {
            content.append(buf, n);
          }
          if (n == 0) {
            for (char& c : content) {
              if (c == '\0') {
                c = ' ';
              }
            }
            while (!content.empty() && content.back() == ' ') {
              content.pop_back();
            }
            result.push_back(std::move(content));
          }
          close(fd);
        }
      }
      break;
    }
    case 11: {
      std::string error;
      std::vector<std::string> entries;
      CHECK(ListDirectoryEntries("/proc", &entries, &error)) << error;
      for (const std::string& entry : entries) {
        if (entry.find_first_not_of("0123456789") == std::string::npos) {
          result.push_back(entry);
        }
      }
      break;
    }
    case 12: {
      pid_t target_pid = 1;
      if (argc > 2) {
        CHECK(absl::SimpleAtoi(argv[2], &target_pid));
      }
      if (kill(target_pid, 0) == -1) {
        result.push_back(errno == EPERM ? "kill_failed:EPERM"
                                        : absl::StrCat("kill_failed:", errno));
      } else {
        result.push_back("kill_success");
      }
      break;
    }
    case 13: {
      if (truncate(argv[2], 0) == 0) {
        result.push_back("truncate_success");
      } else {
        result.push_back(absl::StrCat("truncate_failed:", errno));
      }
      break;
    }
    case 14: {
      if (rename(argv[2], argv[3]) == 0) {
        result.push_back("refer_success");
      } else {
        result.push_back(absl::StrCat("refer_failed:", errno));
      }
      break;
    }
    case 15: {
      std::string link =
          sapi::file_util::fileops::ReadLink("/proc/self/ns/ipc");
      if (link.empty()) {
        return -1;
      }
      result.push_back(link);
      break;
    }
    case 16: {
      int s = socket(AF_UNIX, SOCK_STREAM, 0);
      if (s < 0) {
        result.push_back(absl::StrCat("socket_failed:", errno));
        break;
      }
      struct sockaddr_un addr = {};
      addr.sun_family = AF_UNIX;
      CHECK_LT(strlen(argv[2]), sizeof(addr.sun_path));
      strncpy(addr.sun_path, argv[2], sizeof(addr.sun_path) - 1);
      socklen_t addr_len =
          offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1;
      if (connect(s, reinterpret_cast<struct sockaddr*>(&addr), addr_len) ==
          0) {
        result.push_back("unix_connect_success");
      } else {
        result.push_back(errno == EACCES
                             ? "unix_connect_failed:EACCES"
                             : absl::StrCat("unix_connect_failed:", errno));
      }
      close(s);
      break;
    }
    case 17: {
      int s = socket(AF_UNIX, SOCK_DGRAM, 0);
      if (s < 0) {
        result.push_back(absl::StrCat("socket_failed:", errno));
        break;
      }
      struct sockaddr_un addr = {};
      addr.sun_family = AF_UNIX;
      CHECK_LT(strlen(argv[2]), sizeof(addr.sun_path));
      strncpy(addr.sun_path, argv[2], sizeof(addr.sun_path) - 1);
      socklen_t addr_len =
          offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1;
      if (sendto(s, "x", 1, 0, reinterpret_cast<struct sockaddr*>(&addr),
                 addr_len) == 1) {
        result.push_back("unix_sendto_success");
      } else {
        result.push_back(errno == EACCES
                             ? "unix_sendto_failed:EACCES"
                             : absl::StrCat("unix_sendto_failed:", errno));
      }
      close(s);
      break;
    }
    case 18: {
      int pipefd[2];
      PCHECK(pipe2(pipefd, O_CLOEXEC) == 0);
      pid_t pid = fork();
      PCHECK(pid >= 0);
      if (pid == 0) {
        close(pipefd[0]);
        char* child_argv[] = {argv[2], nullptr};
        execve(argv[2], child_argv, environ);
        int err = errno;
        sapi::file_util::fileops::WriteToFD(
            pipefd[1], reinterpret_cast<const char*>(&err), sizeof(err));
        _exit(1);
      }
      close(pipefd[1]);
      int err = 0;
      ssize_t n = read(pipefd[0], &err, sizeof(err));
      close(pipefd[0]);
      int status = 0;
      PCHECK(waitpid(pid, &status, 0) == pid);
      if (n == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        result.push_back("exec_success");
      } else if (n == sizeof(err)) {
        result.push_back(err == EACCES ? "exec_failed:EACCES"
                                       : absl::StrCat("exec_failed:", err));
      } else {
        result.push_back(absl::StrCat("child_status:", status));
      }
      break;
    }
    case 19: {
      // Test socket creation across allowed and disallowed address families.
      auto check_socket = [&](const char* name, int domain, int type,
                              int protocol) {
        int s = socket(domain, type, protocol);
        if (s >= 0) {
          close(s);
          result.push_back(absl::StrCat(name, ":ok"));
        } else {
          result.push_back(errno == EACCES
                               ? absl::StrCat(name, ":EACCES")
                               : absl::StrCat(name, ":err_", errno));
        }
      };
      check_socket("unix_stream", AF_UNIX, SOCK_STREAM, 0);
      check_socket("unix_dgram", AF_UNIX, SOCK_DGRAM, 0);
      check_socket("inet_tcp", AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
      check_socket("inet_udp", AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
      check_socket("inet6_tcp", AF_INET6, SOCK_STREAM, 0);
      check_socket("inet6_udp", AF_INET6, SOCK_DGRAM, 0);
      check_socket("netlink_route", AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
      check_socket("netlink_usersock", AF_NETLINK, SOCK_DGRAM,
                   NETLINK_USERSOCK);
      check_socket("packet", AF_PACKET, SOCK_DGRAM, 0);
      check_socket("vsock", AF_VSOCK, SOCK_STREAM, 0);
      break;
    }
    case 20: {
      // Test TCP and UDP bind/connect/sendto on 127.0.0.1:<port>.
      int tcp_port = 0;
      int udp_port = 0;
      if (argc > 3) {
        CHECK(absl::SimpleAtoi(argv[2], &tcp_port));
        CHECK(absl::SimpleAtoi(argv[3], &udp_port));
      }
      struct sockaddr_in bind_addr = {};
      bind_addr.sin_family = AF_INET;
      bind_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      bind_addr.sin_port = htons(0);

      int ts = socket(AF_INET, SOCK_STREAM, 0);
      if (ts >= 0) {
        if (bind(ts, reinterpret_cast<struct sockaddr*>(&bind_addr),
                 sizeof(bind_addr)) == 0) {
          result.push_back("tcp_bind:ok");
        } else {
          result.push_back(errno == EACCES
                               ? "tcp_bind:EACCES"
                               : absl::StrCat("tcp_bind:err_", errno));
        }
        close(ts);
      }

      int tc = socket(AF_INET, SOCK_STREAM, 0);
      if (tc >= 0) {
        struct sockaddr_in dst = bind_addr;
        dst.sin_port = htons(tcp_port);
        if (connect(tc, reinterpret_cast<struct sockaddr*>(&dst),
                    sizeof(dst)) == 0) {
          result.push_back("tcp_connect:ok");
        } else {
          result.push_back(errno == EACCES
                               ? "tcp_connect:EACCES"
                               : absl::StrCat("tcp_connect:err_", errno));
        }
        close(tc);
      }

      int us = socket(AF_INET, SOCK_DGRAM, 0);
      if (us >= 0) {
        if (bind(us, reinterpret_cast<struct sockaddr*>(&bind_addr),
                 sizeof(bind_addr)) == 0) {
          result.push_back("udp_bind:ok");
        } else {
          result.push_back(errno == EACCES
                               ? "udp_bind:EACCES"
                               : absl::StrCat("udp_bind:err_", errno));
        }
        close(us);
      }

      int uc = socket(AF_INET, SOCK_DGRAM, 0);
      if (uc >= 0) {
        struct sockaddr_in dst = bind_addr;
        dst.sin_port = htons(udp_port);
        if (connect(uc, reinterpret_cast<struct sockaddr*>(&dst),
                    sizeof(dst)) == 0) {
          result.push_back("udp_connect:ok");
        } else {
          result.push_back(errno == EACCES
                               ? "udp_connect:EACCES"
                               : absl::StrCat("udp_connect:err_", errno));
        }
        close(uc);
      }

      int usend = socket(AF_INET, SOCK_DGRAM, 0);
      if (usend >= 0) {
        struct sockaddr_in dst = bind_addr;
        dst.sin_port = htons(udp_port);
        if (sendto(usend, "x", 1, 0, reinterpret_cast<struct sockaddr*>(&dst),
                   sizeof(dst)) == 1) {
          result.push_back("udp_sendto:ok");
        } else {
          result.push_back(errno == EACCES
                               ? "udp_sendto:EACCES"
                               : absl::StrCat("udp_sendto:err_", errno));
        }
        close(usend);
      }
      break;
    }
    default:
      return 1;
  }

  CHECK(comms.SendUint64(result.size()));
  for (const std::string& entry : result) {
    CHECK(comms.SendString(entry));
  }
  if (mode == 11 || mode == 15) {
    uint32_t ack = 0;
    comms.RecvUint32(&ack);
  }
  return 0;
}
