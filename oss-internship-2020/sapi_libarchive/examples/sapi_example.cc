#include <iostream>

#include "libarchive_sapi.sapi.h"

int main() {
  std::cout << "WORKS2" << std::endl;

  LibarchiveSandbox sandbox;
  sandbox.Init().IgnoreError();
  LibarchiveApi api(&sandbox);

  if (api.archive_write_disk_new().ok()) {
    std::cout << "OK" << std::endl;
  }

  return 0;
}