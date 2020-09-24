#ifndef SAPI_LIBARCHIVE_HELPERS_H
#define SAPI_LIBARCHIVE_HELPERS_H

#include <glog/logging.h>

#include "libarchive_sapi.sapi.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/sandbox2/util/temp_file.h"

inline constexpr size_t kBlockSize = 10240;
inline constexpr size_t kBuffSize = 16384;

// Used to convert the paths provided as arguments for the program
// (the paths used) to an array of absolute paths. This allows the user
// to use either relative or absolute paths
std::vector<std::string> MakeAbsolutePathsVec(const char* argv[]);

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

#endif  // SAPI_LIBARCHIVE_HELPERS_H
