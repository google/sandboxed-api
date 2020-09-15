
#include "helpers.h"

std::string MakeAbsolutePathAtCWD(std::string path) {
    std::string result =  sandbox2::file_util::fileops::MakeAbsolute(path, sandbox2::file_util::fileops::GetCWD());
    CHECK(result != "") << "Could not create absolute path for: " << path;
    return sandbox2::file::CleanPath(result);
}

std::vector<std::string> MakeAbsolutePathsVec(char *argv[]) {
  std::vector<std::string> arr;
  sandbox2::util::CharPtrArrToVecString(argv, &arr);
  std::transform(arr.begin(), arr.end(), arr.begin(), MakeAbsolutePathAtCWD);
  return arr;
}

// std::string GetErrorString(sapi::v::Ptr *archive, LibarchiveSandbox &sandbox, LibarchiveApi &api) {
//     sapi::StatusOr<char *> ret = api.archive_error_string(archive);
//     CHECK(ret.ok() && ret) << "Could not get error message";

//     sapi::StatusOr<std::string> ret2 = sandbox.GetCString(sapi::v::RemotePtr(ret.value()));
//     CHECK(ret.ok()) << "Could not transfer error message";
//     return ret2.value();
// }


std::string CheckStatusAndGetString(const sapi::StatusOr<char *> &status, LibarchiveSandbox &sandbox) {
    CHECK(status.ok() && status.value() != NULL) << "Could not get error message";

    sapi::StatusOr<std::string> ret = sandbox.GetCString(sapi::v::RemotePtr(status.value()));
    CHECK(ret.ok()) << "Could not transfer error message";
    return ret.value();
}

// std::string CallFunctionAndGetString(sapi::v::Ptr *archive, LibarchiveSandbox &sandbox,
// LibarchiveApi *api, sapi::StatusOr<char *> (LibarchiveApi::*func)(sapi::v::Ptr *)) {
//     sapi::StatusOr<char *> ret = (api->*func)(archive);
//     CHECK(ret.ok() && ret) << "Could not get error message";

//     sapi::StatusOr<std::string> ret2 = sandbox.GetCString(sapi::v::RemotePtr(ret.value()));
//     CHECK(ret.ok()) << "Could not transfer error message";
//     return ret2.value();
// }