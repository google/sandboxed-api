// Copyright 2025 Google LLC
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

#ifndef SANDBOXED_API_SANDBOX2_UTIL_ELF_PARSER_H_
#define SANDBOXED_API_SANDBOX2_UTIL_ELF_PARSER_H_

#include <elf.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/config.h"

namespace host_cpu = ::sapi::host_cpu;

namespace sandbox2 {

using ElfEhdr = std::conditional_t<host_cpu::Is64Bit(), Elf64_Ehdr, Elf32_Ehdr>;
using ElfShdr = std::conditional_t<host_cpu::Is64Bit(), Elf64_Shdr, Elf32_Shdr>;
using ElfPhdr = std::conditional_t<host_cpu::Is64Bit(), Elf64_Phdr, Elf32_Phdr>;
using ElfDyn = std::conditional_t<host_cpu::Is64Bit(), Elf64_Dyn, Elf32_Dyn>;
using ElfSym = std::conditional_t<host_cpu::Is64Bit(), Elf64_Sym, Elf32_Sym>;

class ElfParser {
 public:
  // Creates an ElfParser for the given filename.
  static absl::StatusOr<std::unique_ptr<ElfParser>> Create(
      absl::string_view filename);
  ~ElfParser();

  const ElfEhdr& file_header() const { return file_header_; }
  // Reads interpreter path from the ELF file.
  absl::StatusOr<std::string> ReadInterpreter();
  // Reads all symbols from symtab section.
  absl::Status ReadSymbolsFromSymtab(
      const ElfShdr& symtab,
      absl::FunctionRef<void(uintptr_t, absl::string_view)> symbol_callback);
  // Reads all imported libraries from dynamic section.
  absl::Status ReadImportedLibrariesFromDynamic(
      const ElfShdr& dynamic,
      absl::FunctionRef<void(absl::string_view)> library_callback);
  absl::Status ForEachProgram(
      absl::FunctionRef<absl::Status(const ElfPhdr&)> callback);
  absl::Status ForEachSection(
      absl::FunctionRef<absl::Status(const ElfShdr&)> callback);
  // Reads contents of an ELF section.
  absl::StatusOr<std::string> ReadSectionContents(int idx);
  absl::StatusOr<std::string> ReadSectionContents(
      const ElfShdr& section_header);

 private:
  //  Arbitrary cut-off values, so we can parse safely.
  static constexpr int kMaxProgramHeaderEntries = 500;
  static constexpr int kMaxSectionHeaderEntries = 500;
  static constexpr size_t kMaxSectionSize = 500 * 1024 * 1024;
  static constexpr size_t kMaxStrtabSize = 500 * 1024 * 1024;
  static constexpr size_t kMaxLibPathSize = 1024;
  static constexpr int kMaxSymbolEntries = 4 * 1000 * 1000;
  static constexpr int kMaxDynamicEntries = 10000;
  static constexpr size_t kMaxInterpreterSize = 1000;

  FILE* elf_ = nullptr;
  size_t file_size_ = 0;
  bool elf_little_ = false;
  ElfEhdr file_header_;
  std::vector<ElfPhdr> program_headers_;
  std::vector<ElfShdr> section_headers_;
  int symbol_entries_read_ = 0;
  int dynamic_entries_read_ = 0;

  ElfParser() = default;
  ElfParser(const ElfParser&) = delete;
  ElfParser& operator=(const ElfParser&) = delete;

  // Endianess support functions
  uint16_t Load16(const void* absl_nonnull src);
  uint32_t Load32(const void* absl_nonnull src);
  uint64_t Load64(const void* absl_nonnull src);
  template <size_t N>
  void Load(unsigned char (*dst)[N], const void* src);
  template <typename IntT>
  std::enable_if_t<std::is_integral_v<IntT>, void> Load(IntT* dst,
                                                        const void* src);
  // Lazy constructor.
  absl::Status Init(absl::string_view filename);
  // Reads ELF file size.
  absl::Status ReadFileSize();
  // Reads ELF header.
  absl::Status ReadFileHeader();
  // Reads a single ELF program header.
  absl::StatusOr<ElfPhdr> ReadProgramHeader(absl::string_view src);
  // Reads all ELF program headers.
  absl::Status ReadProgramHeaders();
  // Reads a single ELF section header.
  absl::StatusOr<ElfShdr> ReadSectionHeader(absl::string_view src);
  // Reads all ELF section headers.
  absl::Status ReadSectionHeaders();
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_ELF_PARSER_H_
