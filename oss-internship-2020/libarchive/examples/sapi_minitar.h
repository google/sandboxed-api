// Copyright 2020 Google LLC
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

#ifndef SAPI_LIBARCHIVE_EXAMPLES_MINITAR_H
#define SAPI_LIBARCHIVE_EXAMPLES_MINITAR_H

#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>

#include "libarchive_sapi.sapi.h"  // NOLINT(build/include)
#include "sandbox.h"               // NOLINT(build/include)
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/temp_file.h"

// Creates an archive file at the given filename.
absl::Status CreateArchive(const char* filename, int compress,
                           const char** argv, bool verbose = true);

// Extracts an archive file. If do_extract is true, the files will
// be created relative to the current working directory. If do_extract
// is false then the function will just print the entries of the archive.
absl::Status ExtractArchive(const char* filename, int do_extract, int flags,
                            bool verbose = true);

// This function is only called from the "extract function". It is still
// isolated in order to not modify the code structure as much.
absl::StatusOr<int> CopyData(sapi::v::RemotePtr* ar, sapi::v::RemotePtr* aw,
                             LibarchiveApi& api,
                             SapiLibarchiveSandboxExtract& sandbox);

inline constexpr size_t kBlockSize = 10240;
inline constexpr size_t kBuffSize = 16384;

// Converts one string to an absolute path by prepending the current
// working directory to the relative path.
// The path is also cleaned at the end.
std::string MakeAbsolutePathAtCWD(const std::string& path);

// This function takes a status as argument and after checking the status
// it transfers the string. This is used mostly with archive_error_string
// and other library functions that return a char*.
absl::StatusOr<std::string> CheckStatusAndGetString(
    const absl::StatusOr<char*>& status, LibarchiveSandbox& sandbox);

// Creates a temporary directory in the current working directory and
// returns the path. This is used in the extract function where the sandboxed
// process changes the current working directory to this temporary directory.
absl::StatusOr<std::string> CreateTempDirAtCWD();

#endif  // SAPI_LIBARCHIVE_EXAMPLES_MINITAR_H
