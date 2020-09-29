#ifndef SAPI_LIBARCHIVE_MINITAR_H
#define SAPI_LIBARCHIVE_MINITAR_H

#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <glog/logging.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <memory>

#include "libarchive_sapi.sapi.h"
#include "sandbox.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/sandbox2/util/temp_file.h"
#include "sandboxed_api/var_array.h"

void create(const char* filename, int compress, const char** argv,
            bool verbose = true);

void extract(const char* filename, int do_extract, int flags,
             bool verbose = true);

int copy_data(sapi::v::RemotePtr* ar, sapi::v::RemotePtr* aw,
              LibarchiveApi& api, SapiLibarchiveSandboxExtract& sandbox);

inline constexpr size_t kBlockSize = 10240;
inline constexpr size_t kBuffSize = 16384;

// Converts only one string to an absolute path by prepending the current
// working directory to the relative path
std::string MakeAbsolutePathAtCWD(const std::string& path);

// This function takes a status as argument and after checking the status
// it transfers the string. This is used mostly with archive_error_string
// and other library functions that return a char *.
std::string CheckStatusAndGetString(const absl::StatusOr<char*>& status,
                                    LibarchiveSandbox& sandbox);

// Creates a temporary directory in the current working directory and
// returns the path. This is used in the extract function where the sandbox
// changes the current working directory to this temporary directory.
std::string CreateTempDirAtCWD();

#endif  // SAPI_LIBARCHIVE_MINITAR_H
