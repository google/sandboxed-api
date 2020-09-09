
#include "helpers.h"

std::vector<std::string> MakeAbsolutePaths(char *argv[]) {
    std::vector<std::string> arr;
    sandbox2::util::CharPtrArrToVecString(argv, &arr);
    // std::transform(arr.begin(), arr.end(), arr.begin(), [](std::string s) -> std::string {
    //     return sandbox2::file_util::fileops::MakeAbsolute(s, sandbox2::file_util::fileops::GetCWD());
    // });
    auto f = std::bind(sandbox2::file_util::fileops::MakeAbsolute, std::placeholders::_1, sandbox2::file_util::fileops::GetCWD());
    std::transform(arr.begin(), arr.end(), arr.begin(), f);
    return arr;
}