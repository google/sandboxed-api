#ifndef SAPI_LIBARCHIVE_HELPERS_H
#define SAPI_LIBARCHIVE_HELPERS_H

#include "sandboxed_api/sandbox2/util/fileops.h"
#include "sandboxed_api/sandbox2/util.h"


// Used to convert the paths provided as arguments for the program
// (the paths used) to an array of absolute paths. This allows the user
// to use either relative or absolute paths
std::vector<std::string> MakeAbsolutePaths(char *argv[]);

#endif  // SAPI_LIBARCHIVE_HELPERS_H
