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

#ifndef SANDBOXED_API_UTIL_FILEOPS_H_
#define SANDBOXED_API_UTIL_FILEOPS_H_

#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"

namespace sapi::file_util::fileops {

// RAII helper class to automatically close file descriptors.
class FDCloser {
 public:
  explicit FDCloser(int fd = kCanonicalInvalidFd) : fd_{fd} {}
  FDCloser(const FDCloser&) = delete;
  FDCloser& operator=(const FDCloser&) = delete;
  FDCloser(FDCloser&& other) : fd_(other.Release()) {}
  FDCloser& operator=(FDCloser&& other) {
    Swap(other);
    other.Close();
    return *this;
  }
  ~FDCloser();

  int get() const { return fd_; }
  bool Close();
  void Swap(FDCloser& other) { std::swap(fd_, other.fd_); }
  int Release();

 private:
  static constexpr int kCanonicalInvalidFd = -1;

  int fd_;
};

// Returns the current working directory. On error, returns an empty string. Use
// errno/GetLastError() to check the root cause in that case.
std::string GetCWD();

// Returns the target of a symlink. Returns an empty string on failure.
std::string ReadLink(const std::string& filename);

// Reads the absolute path to the symlink target into result. Returns true on
// success. result and filename may be aliased.
bool ReadLinkAbsolute(const std::string& filename, std::string* result);

// Removes the last path component. Returns false if there was no path
// component (path is / or ""). If this function returns false, *output will be
// equal to "/" or "" if the file is absolute or relative, respectively. output
// and file may refer to the same string.
bool RemoveLastPathComponent(const std::string& file, std::string* output);

// Returns a file's basename, i.e.
// If the input path has a trailing slash, the basename is assumed to be
// empty, e.g. StripBasename("/hello/") == "/hello".
// Does no path cleanups; the result is always a prefix/ suffix of the
// passed string.
std::string Basename(absl::string_view path);

// Like above, but returns a file's directory name.
std::string StripBasename(absl::string_view path);

// Tests whether filename exists. If fully_resolve is true, then all symlinks
// are resolved to verify the target exists. Otherwise, this function
// verifies only that the file exists. It may still be a symlink with a
// missing target.
bool Exists(const std::string& filename, bool fully_resolve);

// Reads a directory and fills entries with all the files in that directory.
// On error, false is returned and error is set to a description of the
// error. The filenames in entries are just the basenames of the
// files found.
bool ListDirectoryEntries(const std::string& directory,
                          std::vector<std::string>* entries,
                          std::string* error);

// Deletes the specified file or directory, including any sub-directories.
bool DeleteRecursively(const std::string& filename);

// Copies a file from one location to another. The file will be overwritten  if
// it already exists. If it does not exist, its mode will be new_mode. Returns
// true on success. On failure, a partial copy of the file may remain.
bool CopyFile(const std::string& old_path, const std::string& new_path,
              int new_mode);

// Makes filename absolute with respect to base. Returns an empty string on
// failure.
std::string MakeAbsolute(const std::string& filename, const std::string& base);

// Writes data to a file descriptor. The file descriptor should be blocking.
// Returns true on success.
bool WriteToFD(int fd, const char* data, size_t size);

}  // namespace sapi::file_util::fileops

#endif  // SANDBOXED_API_UTIL_FILEOPS_H_
