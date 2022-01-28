// Copyright 2019 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_UTIL_MINIELF_H_
#define SANDBOXED_API_SANDBOX2_UTIL_MINIELF_H_

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"

namespace sandbox2 {

// Minimal implementation of an ELF file parser to read the program interpreter.
class ElfFile {
 public:
  struct Symbol {
    uintptr_t address;
    std::string name;
  };

  static constexpr uint32_t kGetInterpreter = 1 << 0;
  static constexpr uint32_t kLoadSymbols = 1 << 1;
  static constexpr uint32_t kLoadImportedLibraries = 1 << 2;
  static constexpr uint32_t kAll =
      kGetInterpreter | kLoadSymbols | kLoadImportedLibraries;

  static absl::StatusOr<ElfFile> ParseFromFile(const std::string& filename,
                                               uint32_t features);

  int64_t file_size() const { return file_size_; }
  const std::string& interpreter() const { return interpreter_; }
  const std::vector<Symbol>& symbols() const { return symbols_; }
  const std::vector<std::string>& imported_libraries() const {
    return imported_libraries_;
  }
  bool position_independent() const { return position_independent_; }

 private:
  friend class ElfParser;

  bool position_independent_;
  int64_t file_size_ = 0;
  std::string interpreter_;
  std::vector<Symbol> symbols_;
  std::vector<std::string> imported_libraries_;
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_MINIELF_H_
