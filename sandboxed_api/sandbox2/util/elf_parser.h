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
#include <utility>
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
  // The type used to return ELF file data in the following methods.
  // The string_view holds the data reference and should be used to access the
  // data.
  // Duration of validity of the data and use of the vector depend on the mode
  // in which the parser was opened. If the parser was opened in mmap mode,
  // the data is valid as long as the parser is alive, and the vector is unused.
  // Otherwise, the vector contains the actual data, and string_view is valid
  // as long as the vector is alive.
  // Most users shouldn't depend on these rules and just use the data as long
  // as both parser and the buffer are alive.
  struct Buffer {
    absl::string_view data;
    std::string buffer;
  };

  // Creates an ElfParser for the given filename.
  // If mmap_file is true, the whole file is mmapped for the lifetime of the
  // parser, which makes parsing faster and voids lots of read syscalls and
  // data copying. However, it increases virtual memory consumption.
  // If mmap_file is false, the file is read in small chunks as necessary
  // using seek+read for each chunk.
  static absl::StatusOr<std::unique_ptr<ElfParser>> Create(
      absl::string_view filename, bool mmap_file);

  ~ElfParser();

  const std::string& filename() const { return filename_; }

  const ElfEhdr& file_header() const { return file_header_; }

  // Reads interpreter path from the ELF file.
  absl::StatusOr<std::string> ReadInterpreter();

  // Reads all symbols from symtab section.
  absl::Status ReadSymbolsFromSymtab(
      const ElfShdr& symtab,
      absl::FunctionRef<void(uintptr_t, absl::string_view)> symbol_callback);

  // Reads all imported libraries from dynamic section.
  absl::StatusOr<std::vector<std::string>> ReadImportedLibraries();
  absl::Status ForEachProgram(
      absl::FunctionRef<absl::Status(const ElfPhdr&)> callback);
  absl::Status ForEachSection(
      absl::FunctionRef<absl::Status(absl::string_view, const ElfShdr&)>
          callback);

  // Reads arbitrary data from the ELF file.
  // The method does bounds checks for offset/size, so callers don't need to.
  absl::StatusOr<Buffer> ReadData(size_t offset, size_t size);

  // Reads contents of an ELF section.
  absl::StatusOr<Buffer> ReadSectionContents(int idx);
  absl::StatusOr<Buffer> ReadSectionContents(const ElfShdr& section_header);

 private:
  int fd_ = -1;
  absl::string_view mmap_;
  std::string filename_;
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
  absl::Status Init(absl::string_view filename, bool mmap_file);
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
  absl::Status ReadImportedLibrariesFromDynamic(
      const ElfShdr& dynamic,
      absl::FunctionRef<void(absl::string_view)> library_callback);
};

}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_ELF_PARSER_H_
