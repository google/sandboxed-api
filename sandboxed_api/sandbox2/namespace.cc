// Copyright 2019 Google LLC. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Implementation file for the sandbox2::Namespace class.

#include "sandboxed_api/sandbox2/namespace.h"

#include <fcntl.h>
#include <net/if.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {

static constexpr char kSandbox2ChrootPath[] = "/tmp/.sandbox2chroot";

namespace {
int MountFallbackToReadOnly(const char* source, const char* target,
                            const char* filesystem, uintptr_t flags,
                            const void* data) {
  int rv = mount(source, target, filesystem, flags, data);
  if (rv != 0 && (flags & MS_RDONLY) == 0) {
    SAPI_RAW_LOG(WARNING,
                 "Mounting %s on %s (fs type %s) read-write failed: %s", source,
                 target, filesystem, StrError(errno));
    rv = mount(source, target, filesystem, flags | MS_RDONLY, data);
    if (rv == 0) {
      SAPI_RAW_LOG(INFO, "Mounted %s on %s (fs type %s) as read-only", source,
                   target, filesystem);
    }
  }
  return rv;
}
}  // namespace

void PrepareChroot(const Mounts& mounts) {
  // Create a tmpfs mount for the new rootfs.
  SAPI_RAW_CHECK(util::CreateDirRecursive(kSandbox2ChrootPath, 0700),
                 "could not create directory for rootfs");
  SAPI_RAW_PCHECK(mount("none", kSandbox2ChrootPath, "tmpfs", 0, nullptr) == 0,
                  "mounting rootfs failed");

  // Walk the tree and perform all the mount operations.
  mounts.CreateMounts(kSandbox2ChrootPath);

  SAPI_RAW_PCHECK(
      mount("/", "/", "", MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr) != -1,
      "Remounting / RO failed");
}

void TryDenySetgroups() {
  int fd = open("/proc/self/setgroups", O_WRONLY);
  // We ignore errors since they are most likely due to an old kernel.
  if (fd == -1) {
    return;
  }

  file_util::fileops::FDCloser fd_closer{fd};

  dprintf(fd, "deny");
}

void WriteIDMap(const char* map_path, int32_t uid) {
  int fd = open(map_path, O_WRONLY);
  SAPI_RAW_PCHECK(fd != -1, "Couldn't open %s", map_path);

  file_util::fileops::FDCloser fd_closer{fd};

  SAPI_RAW_PCHECK(dprintf(fd, "1000 %d 1", uid) >= 0,
                  "Could not write %d to %s", uid, map_path);
}

void ActivateLoopbackInterface() {
  ifreq ifreq;

  ifreq.ifr_flags = 0;
  strncpy(ifreq.ifr_name, "lo", IFNAMSIZ);

  // Create an AF_INET6 socket to perform the IF FLAGS ioctls on.
  int fd = socket(AF_INET6, SOCK_DGRAM, 0);
  SAPI_RAW_PCHECK(fd != -1, "creating socket for activating loopback failed");

  file_util::fileops::FDCloser fd_closer{fd};

  // First get the existing flags.
  SAPI_RAW_PCHECK(ioctl(fd, SIOCGIFFLAGS, &ifreq) != -1,
                  "Getting existing flags");

  // From 812 kernels, we don't have CAP_NET_ADMIN anymore. But the interface is
  // already up, so we can skip the next ioctl.
  if (ifreq.ifr_flags & IFF_UP) {
    return;
  }

  // Set the UP flag and write the flags back.
  ifreq.ifr_flags |= IFF_UP;
  SAPI_RAW_PCHECK(ioctl(fd, SIOCSIFFLAGS, &ifreq) != -1, "Setting IFF_UP flag");
}

// Logs the filesystem contents if verbose logging is enabled.
void LogFilesystem(const std::string& dir) {
  std::vector<std::string> entries;
  std::string error;
  if (!file_util::fileops::ListDirectoryEntries(dir, &entries, &error)) {
    SAPI_RAW_PLOG(ERROR, "could not list directory entries for %s", dir);
    return;
  }

  for (const auto& entry : entries) {
    struct stat64 st;
    std::string full_path = file::JoinPath(dir, entry);
    if (lstat64(full_path.c_str(), &st) != 0) {
      SAPI_RAW_PLOG(ERROR, "could not stat %s", full_path);
      continue;
    }

    char ftype;
    switch (st.st_mode & S_IFMT) {
      case S_IFREG:
        ftype = '-';
        break;
      case S_IFDIR:
        ftype = 'd';
        break;
      case S_IFLNK:
        ftype = 'l';
        break;
      default:
        ftype = '?';
        break;
    }

    std::string type_and_mode;
    type_and_mode += ftype;
    type_and_mode += st.st_mode & S_IRUSR ? 'r' : '-';
    type_and_mode += st.st_mode & S_IWUSR ? 'w' : '-';
    type_and_mode += st.st_mode & S_IXUSR ? 'x' : '-';
    type_and_mode += st.st_mode & S_IRGRP ? 'r' : '-';
    type_and_mode += st.st_mode & S_IWGRP ? 'w' : '-';
    type_and_mode += st.st_mode & S_IXGRP ? 'x' : '-';
    type_and_mode += st.st_mode & S_IROTH ? 'r' : '-';
    type_and_mode += st.st_mode & S_IWOTH ? 'w' : '-';
    type_and_mode += st.st_mode & S_IXOTH ? 'x' : '-';

    std::string link;
    if (S_ISLNK(st.st_mode)) {
      link = absl::StrCat(" -> ", file_util::fileops::ReadLink(full_path));
    }
    SAPI_RAW_VLOG(2, "%s %s%s", type_and_mode, full_path, link);

    if (S_ISDIR(st.st_mode)) {
      LogFilesystem(full_path);
    }
  }
}

Namespace::Namespace(bool allow_unrestricted_networking, Mounts mounts,
                     std::string hostname)
    : clone_flags_(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWPID |
                   CLONE_NEWIPC),
      mounts_(std::move(mounts)),
      hostname_(std::move(hostname)) {
  if (!allow_unrestricted_networking) {
    clone_flags_ |= CLONE_NEWNET;
  }
}

void Namespace::DisableUserNamespace() { clone_flags_ &= ~CLONE_NEWUSER; }

int32_t Namespace::GetCloneFlags() const { return clone_flags_; }

void Namespace::InitializeNamespaces(uid_t uid, gid_t gid, int32_t clone_flags,
                                     const Mounts& mounts, bool mount_proc,
                                     const std::string& hostname) {
  if (clone_flags & CLONE_NEWUSER) {
    // Set up the uid and gid map.
    TryDenySetgroups();
    WriteIDMap("/proc/self/uid_map", uid);
    WriteIDMap("/proc/self/gid_map", gid);
  }

  if (!(clone_flags & CLONE_NEWNS)) {
    // CLONE_NEWNS is always set if we're running in namespaces.
    return;
  }

  SAPI_RAW_PCHECK(
      !mount_proc || mount("", "/proc", "proc",
                           MS_NODEV | MS_NOEXEC | MS_NOSUID, nullptr) != -1,
      "Could not mount a new /proc"
  );

  if (clone_flags & CLONE_NEWNET) {
    // Some things can only be done if inside a new network namespace, like
    // mounting /sys, setting a hostname or bringing up lo if necessary.

    SAPI_RAW_PCHECK(
        MountFallbackToReadOnly("", "/sys", "sysfs",
                                MS_NODEV | MS_NOEXEC | MS_NOSUID,
                                nullptr) != -1,
        "Could not mount a new /sys"
    );

    SAPI_RAW_PCHECK(sethostname(hostname.c_str(), hostname.size()) != -1,
                    "Could not set network namespace hostname '%s'", hostname);
    ActivateLoopbackInterface();
  }

  PrepareChroot(mounts);

  // This requires some explanation: It's actually possible to pivot_root('/',
  // '/'). After this operation has been completed, the old root is mounted over
  // the new root, and it's OK to simply umount('/') now, and to have new_root
  // as '/'. This allows us not care about providing any special directory for
  // old_root, which is sometimes not easy, given that e.g. /tmp might not
  // always be present inside new_root.
  SAPI_RAW_PCHECK(
      syscall(__NR_pivot_root, kSandbox2ChrootPath, kSandbox2ChrootPath) != -1,
      "pivot root");
  SAPI_RAW_PCHECK(umount2("/", MNT_DETACH) != -1, "detaching old root");
  SAPI_RAW_PCHECK(chdir("/") == 0, "changing cwd after pivot_root failed");

  if (SAPI_VLOG_IS_ON(2)) {
    SAPI_RAW_VLOG(2, "Dumping the sandboxee's filesystem:");
    LogFilesystem("/");
  }
}

void Namespace::GetNamespaceDescription(NamespaceDescription* pb_description) {
  pb_description->set_clone_flags(clone_flags_);
  *pb_description->mutable_mount_tree_mounts() = mounts_.GetMountTree();
}

}  // namespace sandbox2
