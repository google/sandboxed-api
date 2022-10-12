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
#include <memory>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/strerror.h"

namespace sandbox2 {

namespace file = ::sapi::file;
namespace file_util = ::sapi::file_util;

static constexpr char kSandbox2ChrootPath[] = "/tmp/.sandbox2chroot";

namespace {
int MountFallbackToReadOnly(const char* source, const char* target,
                            const char* filesystem, uintptr_t flags,
                            const void* data) {
  int rv = mount(source, target, filesystem, flags, data);
  if (rv != 0 && (flags & MS_RDONLY) == 0) {
    SAPI_RAW_PLOG(WARNING, "Mounting %s on %s (fs type %s) read-write failed",
                  source, target, filesystem);
    rv = mount(source, target, filesystem, flags | MS_RDONLY, data);
    if (rv == 0) {
      SAPI_RAW_LOG(INFO, "Mounted %s on %s (fs type %s) as read-only", source,
                   target, filesystem);
    }
  }
  return rv;
}

void PrepareChroot(const Mounts& mounts) {
  // Create a tmpfs mount for the new rootfs.
  SAPI_RAW_CHECK(util::CreateDirRecursive(kSandbox2ChrootPath, 0700),
                 "could not create directory for rootfs");
  SAPI_RAW_PCHECK(mount("none", kSandbox2ChrootPath, "tmpfs", 0, nullptr) == 0,
                  "mounting rootfs failed");

  // Walk the tree and perform all the mount operations.
  mounts.CreateMounts(kSandbox2ChrootPath);

  if (mounts.IsRootReadOnly()) {
    // Remount the chroot read-only
    SAPI_RAW_PCHECK(mount(kSandbox2ChrootPath, kSandbox2ChrootPath, "",
                          MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr) == 0,
                    "remounting chroot read-only failed");
  }
}

void TryDenySetgroups() {
  file_util::fileops::FDCloser fd(
      TEMP_FAILURE_RETRY(open("/proc/self/setgroups", O_WRONLY | O_CLOEXEC)));
  // We ignore errors since they are most likely due to an old kernel.
  if (fd.get() == -1) {
    return;
  }

  dprintf(fd.get(), "deny");
}

void WriteIDMap(const char* map_path, int32_t uid) {
  file_util::fileops::FDCloser fd(
      TEMP_FAILURE_RETRY(open(map_path, O_WRONLY | O_CLOEXEC)));
  SAPI_RAW_PCHECK(fd.get() != -1, "Couldn't open %s", map_path);

  SAPI_RAW_PCHECK(dprintf(fd.get(), "1000 %d 1", uid) >= 0,
                  "Could not write %d to %s", uid, map_path);
}

void SetupIDMaps(uid_t uid, gid_t gid) {
  TryDenySetgroups();
  WriteIDMap("/proc/self/uid_map", uid);
  WriteIDMap("/proc/self/gid_map", gid);
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
    SAPI_RAW_VLOG(2, "%s %s%s", type_and_mode.c_str(), full_path.c_str(),
                  link.c_str());

    if (S_ISDIR(st.st_mode)) {
      LogFilesystem(full_path);
    }
  }
}

}  // namespace

Namespace::Namespace(bool allow_unrestricted_networking, Mounts mounts,
                     std::string hostname, bool allow_mount_propagation)
    : clone_flags_(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWPID |
                   CLONE_NEWIPC),
      mounts_(std::move(mounts)),
      hostname_(std::move(hostname)),
      allow_mount_propagation_(allow_mount_propagation) {
  if (!allow_unrestricted_networking) {
    clone_flags_ |= CLONE_NEWNET;
  }
}

void Namespace::DisableUserNamespace() { clone_flags_ &= ~CLONE_NEWUSER; }

int32_t Namespace::GetCloneFlags() const { return clone_flags_; }

void Namespace::InitializeNamespaces(uid_t uid, gid_t gid, int32_t clone_flags,
                                     const Mounts& mounts, bool mount_proc,
                                     const std::string& hostname,
                                     bool avoid_pivot_root,
                                     bool allow_mount_propagation) {
  if (clone_flags & CLONE_NEWUSER && !avoid_pivot_root) {
    SetupIDMaps(uid, gid);
  }

  if (!(clone_flags & CLONE_NEWNS)) {
    // CLONE_NEWNS is always set if we're running in namespaces.
    return;
  }

  std::unique_ptr<file_util::fileops::FDCloser> root_fd;
  if (avoid_pivot_root) {
    // We want to bind-mount chrooted to real root, so that symlinks work.
    // Reference to main root is kept to escape later from the chroot
    root_fd = std::make_unique<file_util::fileops::FDCloser>(
        TEMP_FAILURE_RETRY(open("/", O_PATH)));
    SAPI_RAW_CHECK(root_fd->get() != -1, "creating fd for main root");

    SAPI_RAW_PCHECK(chroot("/realroot") != -1, "chrooting to real root");
    SAPI_RAW_PCHECK(chdir("/") != -1, "chdir / after chrooting real root");
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

  if (avoid_pivot_root) {
    // Keep a reference to /proc/self as it might not be mounted later
    file_util::fileops::FDCloser proc_self_fd(
        TEMP_FAILURE_RETRY(open("/proc/self/", O_PATH)));
    SAPI_RAW_PCHECK(proc_self_fd.get() != -1, "opening /proc/self");

    // Return to the main root
    SAPI_RAW_PCHECK(fchdir(root_fd->get()) != -1, "chdir to main root");
    SAPI_RAW_PCHECK(chroot(".") != -1, "chrooting to main root");
    SAPI_RAW_PCHECK(chdir("/") != -1, "chdir / after chrooting main root");

    // Get a refrence to /realroot to umount it later
    file_util::fileops::FDCloser realroot_fd(
        TEMP_FAILURE_RETRY(open("/realroot", O_PATH)));

    // Move the chroot out of realroot to /
    std::string chroot_path = file::JoinPath("/realroot", kSandbox2ChrootPath);
    SAPI_RAW_PCHECK(chdir(chroot_path.c_str()) != -1, "chdir to chroot");
    SAPI_RAW_PCHECK(mount(".", "/", "", MS_MOVE, nullptr) == 0,
                    "moving rootfs failed");
    SAPI_RAW_PCHECK(chroot(".") != -1, "chrooting moved chroot");
    SAPI_RAW_PCHECK(chdir("/") != -1, "chdir / after chroot");

    // Umount the realroot so that no reference is left
    SAPI_RAW_PCHECK(fchdir(realroot_fd.get()) != -1, "fchdir to /realroot");
    SAPI_RAW_PCHECK(umount2(".", MNT_DETACH) != -1, "detaching old root");

    if (clone_flags & CLONE_NEWUSER) {
      // Also CLONE_NEWNS so that / mount becomes locked
      SAPI_RAW_PCHECK(unshare(CLONE_NEWUSER | CLONE_NEWNS) != -1,
                      "unshare(CLONE_NEWUSER | CLONE_NEWNS)");
      // Setup ID maps using reference to /proc/self obatined earlier
      file_util::fileops::FDCloser setgroups_fd(TEMP_FAILURE_RETRY(
          openat(proc_self_fd.get(), "setgroups", O_WRONLY | O_CLOEXEC)));
      // We ignore errors since they are most likely due to an old kernel.
      if (setgroups_fd.get() != -1) {
        dprintf(setgroups_fd.get(), "deny");
      }
      file_util::fileops::FDCloser uid_map_fd(
          TEMP_FAILURE_RETRY(openat(proc_self_fd.get(), "uid_map", O_WRONLY)));
      SAPI_RAW_PCHECK(uid_map_fd.get() != -1, "Couldn't open uid_map");
      SAPI_RAW_PCHECK(dprintf(uid_map_fd.get(), "1000 1000 1") >= 0,
                      "Could not write uid_map");
      file_util::fileops::FDCloser gid_map_fd(
          TEMP_FAILURE_RETRY(openat(proc_self_fd.get(), "gid_map", O_WRONLY)));
      SAPI_RAW_PCHECK(gid_map_fd.get() != -1, "Couldn't open gid_map");
      SAPI_RAW_PCHECK(dprintf(gid_map_fd.get(), "1000 1000 1") >= 0,
                      "Could not write gid_map");
    }
  } else {
    // This requires some explanation: It's actually possible to pivot_root('/',
    // '/'). After this operation has been completed, the old root is mounted
    // over the new root, and it's OK to simply umount('/') now, and to have
    // new_root as '/'. This allows us not care about providing any special
    // directory for old_root, which is sometimes not easy, given that e.g. /tmp
    // might not always be present inside new_root.
    SAPI_RAW_PCHECK(syscall(__NR_pivot_root, kSandbox2ChrootPath,
                            kSandbox2ChrootPath) != -1,
                    "pivot root");
    SAPI_RAW_PCHECK(umount2("/", MNT_DETACH) != -1, "detaching old root");
  }

  SAPI_RAW_PCHECK(chdir("/") == 0,
                  "changing cwd after mntns initialization failed");

  if (allow_mount_propagation) {
    SAPI_RAW_PCHECK(mount("/", "/", "", MS_SLAVE | MS_REC, nullptr) == 0,
                    "changing mount propagation to slave failed");
  } else {
    SAPI_RAW_PCHECK(mount("/", "/", "", MS_PRIVATE | MS_REC, nullptr) == 0,
                    "changing mount propagation to private failed");
  }

  if (SAPI_VLOG_IS_ON(2)) {
    SAPI_RAW_VLOG(2, "Dumping the sandboxee's filesystem:");
    LogFilesystem("/");
  }
}

void Namespace::InitializeInitialNamespaces(uid_t uid, gid_t gid) {
  SetupIDMaps(uid, gid);
  SAPI_RAW_CHECK(util::CreateDirRecursive(kSandbox2ChrootPath, 0700),
                 "could not create directory for rootfs");
  SAPI_RAW_PCHECK(mount("none", kSandbox2ChrootPath, "tmpfs", 0, nullptr) == 0,
                  "mounting rootfs failed");
  auto realroot_path = file::JoinPath(kSandbox2ChrootPath, "/realroot");
  SAPI_RAW_CHECK(util::CreateDirRecursive(realroot_path, 0700),
                 "could not create directory for real root");
  SAPI_RAW_PCHECK(syscall(__NR_pivot_root, kSandbox2ChrootPath,
                          realroot_path.c_str()) != -1,
                  "pivot root");
  SAPI_RAW_PCHECK(symlink("/realroot/proc", "/proc") != -1, "symlinking /proc");
  SAPI_RAW_PCHECK(
      mount("/", "/", "", MS_BIND | MS_REMOUNT | MS_RDONLY, nullptr) == 0,
      "remounting rootfs read-only failed");
}

void Namespace::GetNamespaceDescription(NamespaceDescription* pb_description) {
  pb_description->set_clone_flags(clone_flags_);
  *pb_description->mutable_mount_tree_mounts() = mounts_.GetMountTree();
}

}  // namespace sandbox2
