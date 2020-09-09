#include <iostream>

#include <archive.h>
#include <archive_entry.h>
#include "helpers.h"

int main(int argc, char *argv[]) {
    std::cout << "WORKS" << std::endl;
    std::vector<std::string> s = MakeAbsolutePaths(argv + 1);
    std::cout << "ok" << std::endl;
    for (const auto &i : s) {
        std::cout << i << std::endl;
    }
    std::cout << "==========" << std::endl;
    return 0;
}