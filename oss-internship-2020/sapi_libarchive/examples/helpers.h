#ifndef SAPI_LIBARCHIVE_HELPERS_H
#define SAPI_LIBARCHIVE_HELPERS_H

#include <glog/logging.h>

#include "libarchive_sapi.sapi.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util/path.h"
#include "sandboxed_api/sandbox2/util/temp_file.h"

// Used to convert the paths provided as arguments for the program
// (the paths used) to an array of absolute paths. This allows the user
// to use either relative or absolute paths
std::vector<std::string> MakeAbsolutePathsVec(const char *argv[]);

// Converts only one string to an absolute path by prepending the current
// working directory to the relative path
std::string MakeAbsolutePathAtCWD(const std::string &path);

// Calls the archive_error_string and returns the mesage after it was
// transferred to the client process. std::string GetErrorString(sapi::v::Ptr
// *archive, LibarchiveSandbox &sandbox, LibarchiveApi &api);

std::string CheckStatusAndGetString(const sapi::StatusOr<char *> &status,
                                    LibarchiveSandbox &sandbox);

// std::string CallFunctionAndGetString(sapi::v::Ptr *archive, LibarchiveSandbox
// &sandbox, LibarchiveApi *api, sapi::StatusOr<char *>
// (LibarchiveApi::*func)(sapi::v::Ptr *));

std::string CreateTempDirAtCWD();

#endif  // SAPI_LIBARCHIVE_HELPERS_H
