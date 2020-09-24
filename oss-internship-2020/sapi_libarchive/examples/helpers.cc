
#include "helpers.h"

std::string MakeAbsolutePathAtCWD(const std::string& path) {
  std::string result = sandbox2::file_util::fileops::MakeAbsolute(
      path, sandbox2::file_util::fileops::GetCWD());
  CHECK(result != "") << "Could not create absolute path for: " << path;
  return sandbox2::file::CleanPath(result);
}

std::vector<std::string> MakeAbsolutePathsVec(const char* argv[]) {
  std::vector<std::string> arr;
  sandbox2::util::CharPtrArrToVecString(const_cast<char* const*>(argv), &arr);
  std::transform(arr.begin(), arr.end(), arr.begin(), MakeAbsolutePathAtCWD);
  return arr;
}

std::string CheckStatusAndGetString(const absl::StatusOr<char*>& status,
                                    LibarchiveSandbox& sandbox) {
  CHECK(status.ok() && status.value() != NULL) << "Could not get error message";

  absl::StatusOr<std::string> ret =
      sandbox.GetCString(sapi::v::RemotePtr(status.value()));
  CHECK(ret.ok()) << "Could not transfer error message";
  return ret.value();
}

std::string CreateTempDirAtCWD() {
  std::string cwd = sandbox2::file_util::fileops::GetCWD();
  CHECK(!cwd.empty()) << "Could not get current working directory";
  cwd.append("/");

  absl::StatusOr<std::string> result = sandbox2::CreateTempDir(cwd);
  CHECK(result.ok()) << "Could not create temporary directory at " << cwd;
  return result.value();
}
