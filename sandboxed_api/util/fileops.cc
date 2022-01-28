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

#include "sandboxed_api/util/fileops.h"

#include <dirent.h>    // DIR
#include <limits.h>    // PATH_MAX
#include <sys/stat.h>  // stat64
#include <unistd.h>

#include <fstream>
#include <memory>

#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "sandboxed_api/util/strerror.h"

namespace sapi::file_util::fileops {

FDCloser::~FDCloser() { Close(); }

bool FDCloser::Close() {
  int fd = Release();
  if (fd == kCanonicalInvalidFd) {
    return false;
  }
  return close(fd) == 0 || errno == EINTR;
}

int FDCloser::Release() {
  int ret = fd_;
  fd_ = kCanonicalInvalidFd;
  return ret;
}

bool GetCWD(std::string* result) {
  // Calling getcwd() with a nullptr buffer is a commonly implemented extension.
  std::unique_ptr<char, void (*)(char*)> cwd(getcwd(nullptr, 0),
                                             [](char* p) { free(p); });
  if (!cwd) {
    return false;
  }
  *result = cwd.get();
  return true;
}

std::string GetCWD() {
  std::string cwd;
  GetCWD(&cwd);
  return cwd;
}

// Makes a path absolute with respect to base. Returns true on success. Result
// may be an alias of base or filename.
bool MakeAbsolute(const std::string& filename, const std::string& base,
                  std::string* result) {
  if (filename.empty()) {
    return false;
  }
  if (filename[0] == '/') {
    if (result != &filename) {
      *result = filename;
    }
    return true;
  }

  std::string actual_base = base;
  if (actual_base.empty() && !GetCWD(&actual_base)) {
    return false;
  }

  actual_base = std::string(absl::StripSuffix(actual_base, "/"));

  if (filename == ".") {
    if (actual_base.empty()) {
      *result = "/";
    } else {
      *result = actual_base;
    }
  } else {
    *result = actual_base + "/" + filename;
  }
  return true;
}

std::string MakeAbsolute(const std::string& filename, const std::string& base) {
  std::string result;
  return !MakeAbsolute(filename, base, &result) ? "" : result;
}

bool RemoveLastPathComponent(const std::string& file, std::string* output) {
  // Point idx at the last non-slash in the string. This should mark the last
  // character of the base name.
  auto idx = file.find_last_not_of('/');
  // If no non-slash is found, we have all slashes or an empty string. Return
  // the appropriate value and false to indicate there was no path component to
  // remove.
  if (idx == std::string::npos) {
    if (file.empty()) {
      output->clear();
    } else {
      *output = "/";
    }
    return false;
  }

  // Otherwise, we have to trim the last path component. Find where it begins.
  // Point idx at the last slash before the base name.
  idx = file.find_last_of('/', idx);
  // If we don't find a slash, then we have something of the form "file/*", so
  // just return the empty string.
  if (idx == std::string::npos) {
    output->clear();
  } else {
    // Then find the last character that isn't a slash, in case the slash is
    // repeated.
    // Point idx at the character at the last character of the path component
    // that precedes the base name. I.e. if you have /foo/bar, idx will point
    // at the last "o" in foo. We remove everything after this index.
    idx = file.find_last_not_of('/', idx);
    // If none is found, then set idx to 0 so the below code will leave the
    // first slash.
    if (idx == std::string::npos) idx = 0;
    // This is an optimization to prevent a copy if output and file are
    // aliased.
    if (&file == output) {
      output->erase(idx + 1, std::string::npos);
    } else {
      output->assign(file, 0, idx + 1);
    }
  }
  return true;
}

std::string ReadLink(const std::string& filename) {
  std::string result(PATH_MAX, '\0');
  const auto size = readlink(filename.c_str(), &result[0], PATH_MAX);
  if (size < 0) {
    return "";
  }
  result.resize(size);
  return result;
}

bool ReadLinkAbsolute(const std::string& filename, std::string* result) {
  std::string base_dir;

  // Do this first. Otherwise, if &filename == result, we won't be able to find
  // it after the ReadLink call.
  RemoveLastPathComponent(filename, &base_dir);

  std::string link = ReadLink(filename);
  if (link.empty()) {
    return false;
  }
  *result = std::move(link);

  // Need two calls in case filename itself is relative.
  return MakeAbsolute(MakeAbsolute(*result, base_dir), "", result);
}

std::string Basename(absl::string_view path) {
  const auto last_slash = path.find_last_of('/');
  return std::string(last_slash == std::string::npos
                         ? path
                         : absl::ClippedSubstr(path, last_slash + 1));
}

std::string StripBasename(absl::string_view path) {
  const auto last_slash = path.find_last_of('/');
  if (last_slash == std::string::npos) {
    return "";
  }
  if (last_slash == 0) {
    return "/";
  }
  return std::string(path.substr(0, last_slash));
}

bool Exists(const std::string& filename, bool fully_resolve) {
  struct stat64 st;
  return (fully_resolve ? stat64(filename.c_str(), &st)
                        : lstat64(filename.c_str(), &st)) != -1;
}

bool ListDirectoryEntries(const std::string& directory,
                          std::vector<std::string>* entries,
                          std::string* error) {
  errno = 0;
  std::unique_ptr<DIR, void (*)(DIR*)> dir{opendir(directory.c_str()),
                                           [](DIR* d) { closedir(d); }};
  if (!dir) {
    *error = absl::StrCat("opendir(", directory, "): ", StrError(errno));
    return false;
  }

  errno = 0;
  struct dirent* entry;
  while ((entry = readdir(dir.get())) != nullptr) {
    const std::string name(entry->d_name);
    if (name != "." && name != "..") {
      entries->push_back(name);
    }
  }
  if (errno != 0) {
    *error = absl::StrCat("readdir(", directory, "): ", StrError(errno));
    return false;
  }
  return true;
}

bool DeleteRecursively(const std::string& filename) {
  std::vector<std::string> to_delete;
  to_delete.push_back(filename);

  while (!to_delete.empty()) {
    const std::string delfile = to_delete.back();

    struct stat64 st;
    if (lstat64(delfile.c_str(), &st) == -1) {
      if (errno == ENOENT) {
        // Most likely the first file. Either that or someone is deleting the
        // files out from under us.
        to_delete.pop_back();
        continue;
      }
      return false;
    }

    if (S_ISDIR(st.st_mode)) {
      if (rmdir(delfile.c_str()) != 0 && errno != ENOENT) {
        if (errno == ENOTEMPTY) {
          std::string error;
          std::vector<std::string> entries;
          if (!ListDirectoryEntries(delfile, &entries, &error)) {
            return false;
          }
          for (const auto& entry : entries) {
            to_delete.push_back(delfile + "/" + entry);
          }
        } else {
          return false;
        }
      } else {
        to_delete.pop_back();
      }
    } else {
      if (unlink(delfile.c_str()) != 0 && errno != ENOENT) {
        return false;
      }
      to_delete.pop_back();
    }
  }
  return true;
}

bool CopyFile(const std::string& old_path, const std::string& new_path,
              int new_mode) {
  {
    std::ifstream input(old_path, std::ios_base::binary);
    std::ofstream output(new_path,
                         std::ios_base::trunc | std::ios_base::binary);
    output << input.rdbuf();
    if (!input || !output) {
      return false;
    }
  }
  return chmod(new_path.c_str(), new_mode) == 0;
}

bool WriteToFD(int fd, const char* data, size_t size) {
  while (size > 0) {
    ssize_t result = TEMP_FAILURE_RETRY(write(fd, data, size));
    if (result <= 0) {
      return false;
    }
    size -= result;
    data += result;
  }
  return true;
}

}  // namespace sapi::file_util::fileops
