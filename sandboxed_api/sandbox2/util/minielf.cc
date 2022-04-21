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

#include "sandboxed_api/sandbox2/util/minielf.h"

#include <elf.h>

#include <cstddef>
#include <memory>
#include <type_traits>

#include "absl/base/internal/endian.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status_macros.h"

namespace host_cpu = ::sapi::host_cpu;

namespace sandbox2 {

using ElfEhdr = std::conditional_t<host_cpu::Is64Bit(), Elf64_Ehdr, Elf32_Ehdr>;
using ElfShdr = std::conditional_t<host_cpu::Is64Bit(), Elf64_Shdr, Elf32_Shdr>;
using ElfPhdr = std::conditional_t<host_cpu::Is64Bit(), Elf64_Phdr, Elf32_Phdr>;
using ElfDyn = std::conditional_t<host_cpu::Is64Bit(), Elf64_Dyn, Elf32_Dyn>;
using ElfSym = std::conditional_t<host_cpu::Is64Bit(), Elf64_Sym, Elf32_Sym>;

constexpr int kElfHeaderSize = sizeof(ElfEhdr);  // Maximum size for binaries

constexpr char kElfMagic[] =
    "\x7F"
    "ELF";

constexpr int kEiClassOffset = 0x04;
constexpr int kEiClass = host_cpu::Is64Bit() ? ELFCLASS64 : ELFCLASS32;

constexpr int kEiDataOffset = 0x05;
constexpr int kEiDataLittle = 1;  // Little Endian
constexpr int kEiDataBig = 2;     // Big Endian

constexpr int kEiVersionOffset = 0x06;
constexpr int kEvCurrent = 1;  // ELF version

namespace {

// NOLINTNEXTLINE
absl::Status CheckedFSeek(FILE* f, long offset, int whence) {
  if (fseek(f, offset, whence)) {
    return absl::ErrnoToStatus(errno, "Fseek on ELF failed");
  }
  return absl::OkStatus();
}

absl::Status CheckedFRead(void* dst, size_t size, size_t nmemb, FILE* f) {
  if (std::fread(dst, size, nmemb, f) == nmemb) {
    return absl::OkStatus();
  }
  return absl::ErrnoToStatus(errno, "Reading ELF data failed");
}

absl::Status CheckedRead(std::string* s, FILE* f) {
  return CheckedFRead(&(*s)[0], 1, s->size(), f);
}

absl::string_view ReadName(uint32_t offset, absl::string_view strtab) {
  auto name = strtab.substr(offset);
  return name.substr(0, name.find('\0'));
}

}  //  namespace

#define LOAD_MEMBER(data_struct, member, src)                            \
  Load(&(data_struct).member,                                            \
       &src[offsetof(std::remove_reference<decltype(data_struct)>::type, \
                     member)])

class ElfParser {
 public:
  // Arbitrary cut-off values, so we can parse safely.
  static constexpr int kMaxProgramHeaderEntries = 500;
  static constexpr int kMaxSectionHeaderEntries = 500;
  static constexpr size_t kMaxSectionSize = 200 * 1024 * 1024;
  static constexpr size_t kMaxStrtabSize = 500 * 1024 * 1024;
  static constexpr size_t kMaxLibPathSize = 1024;
  static constexpr int kMaxSymbolEntries = 2 * 1000 * 1000;
  static constexpr int kMaxDynamicEntries = 10000;
  static constexpr size_t kMaxInterpreterSize = 1000;

  static absl::StatusOr<ElfFile> Parse(const std::string& filename,
                                       uint32_t features);

  ~ElfParser() {
    if (elf_) {
      std::fclose(elf_);
    }
  }

 private:
  ElfParser() = default;

  // Endianess support functions
  uint16_t Load16(const void* src) {
    return elf_little_ ? absl::little_endian::Load16(src)
                       : absl::big_endian::Load16(src);
  }
  uint32_t Load32(const void* src) {
    return elf_little_ ? absl::little_endian::Load32(src)
                       : absl::big_endian::Load32(src);
  }
  uint64_t Load64(const void* src) {
    return elf_little_ ? absl::little_endian::Load64(src)
                       : absl::big_endian::Load64(src);
  }

  template <size_t N>
  void Load(unsigned char (*dst)[N], const void* src) {
    memcpy(dst, src, N);
  }

  template <typename IntT>
  std::enable_if_t<std::is_integral_v<IntT>, void> Load(IntT* dst,
                                                        const void* src) {
    switch (sizeof(IntT)) {
      case 1:
        *dst = *reinterpret_cast<const char*>(src);
        break;
      case 2:
        *dst = Load16(src);
        break;
      case 4:
        *dst = Load32(src);
        break;
      case 8:
        *dst = Load64(src);
        break;
    }
  }

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
  // Reads contents of an ELF section.
  absl::StatusOr<std::string> ReadSectionContents(int idx);
  absl::StatusOr<std::string> ReadSectionContents(
      const ElfShdr& section_header);
  // Reads all symbols from symtab section.
  absl::Status ReadSymbolsFromSymtab(const ElfShdr& symtab);
  // Reads all imported libraries from dynamic section.
  absl::Status ReadImportedLibrariesFromDynamic(const ElfShdr& dynamic);

  ElfFile result_;
  FILE* elf_ = nullptr;
  size_t file_size_ = 0;
  bool elf_little_ = false;
  ElfEhdr file_header_;
  std::vector<ElfPhdr> program_headers_;
  std::vector<ElfShdr> section_headers_;

  int symbol_entries_read = 0;
  int dynamic_entries_read = 0;
};

absl::Status ElfParser::ReadFileSize() {
  std::fseek(elf_, 0, SEEK_END);
  file_size_ = std::ftell(elf_);
  if (file_size_ < kElfHeaderSize) {
    return absl::FailedPreconditionError(
        absl::StrCat("file too small: ", file_size_, " bytes, at least ",
                     kElfHeaderSize, " bytes expected"));
  }
  return absl::OkStatus();
}

absl::Status ElfParser::ReadFileHeader() {
  std::string header(kElfHeaderSize, '\0');
  SAPI_RETURN_IF_ERROR(CheckedFSeek(elf_, 0, SEEK_SET));
  SAPI_RETURN_IF_ERROR(CheckedRead(&header, elf_));

  if (!absl::StartsWith(header, kElfMagic)) {
    return absl::FailedPreconditionError("magic not found, not an ELF");
  }

  if (header[kEiClassOffset] != kEiClass) {
    return absl::FailedPreconditionError("invalid ELF class");
  }
  const auto elf_data = header[kEiDataOffset];
  elf_little_ = elf_data == kEiDataLittle;
  if (!elf_little_ && elf_data != kEiDataBig) {
    return absl::FailedPreconditionError("invalid endianness");
  }

  if (header[kEiVersionOffset] != kEvCurrent) {
    return absl::FailedPreconditionError("invalid ELF version");
  }
  LOAD_MEMBER(file_header_, e_ident, header.data());
  LOAD_MEMBER(file_header_, e_type, header.data());
  LOAD_MEMBER(file_header_, e_machine, header.data());
  LOAD_MEMBER(file_header_, e_version, header.data());
  LOAD_MEMBER(file_header_, e_entry, header.data());
  LOAD_MEMBER(file_header_, e_phoff, header.data());
  LOAD_MEMBER(file_header_, e_shoff, header.data());
  LOAD_MEMBER(file_header_, e_flags, header.data());
  LOAD_MEMBER(file_header_, e_ehsize, header.data());
  LOAD_MEMBER(file_header_, e_phentsize, header.data());
  LOAD_MEMBER(file_header_, e_phnum, header.data());
  LOAD_MEMBER(file_header_, e_shentsize, header.data());
  LOAD_MEMBER(file_header_, e_shnum, header.data());
  LOAD_MEMBER(file_header_, e_shstrndx, header.data());
  return absl::OkStatus();
}

absl::StatusOr<ElfShdr> ElfParser::ReadSectionHeader(absl::string_view src) {
  if (src.size() < sizeof(ElfShdr)) {
    return absl::FailedPreconditionError(
        absl::StrCat("invalid section header data: got ", src.size(),
                     " bytes, ", sizeof(ElfShdr), " bytes expected."));
  }
  ElfShdr rv;
  LOAD_MEMBER(rv, sh_name, src.data());
  LOAD_MEMBER(rv, sh_type, src.data());
  LOAD_MEMBER(rv, sh_flags, src.data());
  LOAD_MEMBER(rv, sh_addr, src.data());
  LOAD_MEMBER(rv, sh_offset, src.data());
  LOAD_MEMBER(rv, sh_size, src.data());
  LOAD_MEMBER(rv, sh_link, src.data());
  LOAD_MEMBER(rv, sh_info, src.data());
  LOAD_MEMBER(rv, sh_addralign, src.data());
  LOAD_MEMBER(rv, sh_entsize, src.data());
  return rv;
}

absl::Status ElfParser::ReadSectionHeaders() {
  if (file_header_.e_shoff > file_size_) {
    return absl::FailedPreconditionError(
        absl::StrCat("invalid section header offset: ", file_header_.e_shoff));
  }
  if (file_header_.e_shentsize != sizeof(ElfShdr)) {
    return absl::FailedPreconditionError(absl::StrCat(
        "section header entry size incorrect: ", file_header_.e_shentsize,
        " bytes, ", sizeof(ElfShdr), " expected."));
  }
  if (file_header_.e_shnum > kMaxSectionHeaderEntries) {
    return absl::FailedPreconditionError(
        absl::StrCat("too many section header entries: ", file_header_.e_shnum,
                     " limit: ", kMaxSectionHeaderEntries));
  }
  std::string headers(file_header_.e_shentsize * file_header_.e_shnum, '\0');
  SAPI_RETURN_IF_ERROR(CheckedFSeek(elf_, file_header_.e_shoff, SEEK_SET));
  SAPI_RETURN_IF_ERROR(CheckedRead(&headers, elf_));
  section_headers_.resize(file_header_.e_shnum);
  absl::string_view src = headers;
  for (int i = 0; i < file_header_.e_shnum; ++i) {
    SAPI_ASSIGN_OR_RETURN(section_headers_[i], ReadSectionHeader(src));
    src = src.substr(file_header_.e_shentsize);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> ElfParser::ReadSectionContents(int idx) {
  if (idx < 0 || idx >= section_headers_.size()) {
    return absl::FailedPreconditionError(
        absl::StrCat("invalid section header index: ", idx));
  }
  return ReadSectionContents(section_headers_.at(idx));
}

absl::StatusOr<std::string> ElfParser::ReadSectionContents(
    const ElfShdr& section_header) {
  auto offset = section_header.sh_offset;
  if (offset > file_size_) {
    return absl::FailedPreconditionError(
        absl::StrCat("invalid section offset: ", offset));
  }
  auto size = section_header.sh_size;
  if (size > kMaxSectionSize) {
    return absl::FailedPreconditionError(
        absl::StrCat("section too big: ", size, " limit: ", kMaxSectionSize));
  }
  std::string rv(size, '\0');
  SAPI_RETURN_IF_ERROR(CheckedFSeek(elf_, offset, SEEK_SET));
  SAPI_RETURN_IF_ERROR(CheckedRead(&rv, elf_));
  return rv;
}

absl::StatusOr<ElfPhdr> ElfParser::ReadProgramHeader(absl::string_view src) {
  if (src.size() < sizeof(ElfPhdr)) {
    return absl::FailedPreconditionError(
        absl::StrCat("invalid program header data: got ", src.size(),
                     " bytes, ", sizeof(ElfPhdr), " bytes expected."));
  }
  ElfPhdr rv;
  LOAD_MEMBER(rv, p_type, src.data());
  LOAD_MEMBER(rv, p_flags, src.data());
  LOAD_MEMBER(rv, p_offset, src.data());
  LOAD_MEMBER(rv, p_vaddr, src.data());
  LOAD_MEMBER(rv, p_paddr, src.data());
  LOAD_MEMBER(rv, p_filesz, src.data());
  LOAD_MEMBER(rv, p_memsz, src.data());
  LOAD_MEMBER(rv, p_align, src.data());
  return rv;
}

absl::Status ElfParser::ReadProgramHeaders() {
  if (file_header_.e_phoff > file_size_) {
    return absl::FailedPreconditionError(
        absl::StrCat("invalid program header offset: ", file_header_.e_phoff));
  }
  if (file_header_.e_phentsize != sizeof(ElfPhdr)) {
    return absl::FailedPreconditionError(absl::StrCat(
        "section header entry size incorrect: ", file_header_.e_phentsize,
        " bytes, ", sizeof(ElfPhdr), " expected."));
  }
  if (file_header_.e_phnum > kMaxProgramHeaderEntries) {
    return absl::FailedPreconditionError(
        absl::StrCat("too many program header entries: ", file_header_.e_phnum,
                     " limit: ", kMaxProgramHeaderEntries));
  }
  std::string headers(file_header_.e_phentsize * file_header_.e_phnum, '\0');
  SAPI_RETURN_IF_ERROR(CheckedFSeek(elf_, file_header_.e_phoff, SEEK_SET));
  SAPI_RETURN_IF_ERROR(CheckedRead(&headers, elf_));
  program_headers_.resize(file_header_.e_phnum);
  absl::string_view src = headers;
  for (int i = 0; i < file_header_.e_phnum; ++i) {
    SAPI_ASSIGN_OR_RETURN(program_headers_[i], ReadProgramHeader(src));
    src = src.substr(file_header_.e_phentsize);
  }
  return absl::OkStatus();
}

absl::Status ElfParser::ReadSymbolsFromSymtab(const ElfShdr& symtab) {
  if (symtab.sh_type != SHT_SYMTAB) {
    return absl::FailedPreconditionError("invalid symtab type");
  }
  if (symtab.sh_entsize != sizeof(ElfSym)) {
    return absl::InternalError(
        absl::StrCat("invalid symbol entry size: ", symtab.sh_entsize));
  }
  if ((symtab.sh_size % symtab.sh_entsize) != 0) {
    return absl::InternalError(
        absl::StrCat("invalid symbol table size: ", symtab.sh_size));
  }
  size_t symbol_entries = symtab.sh_size / symtab.sh_entsize;
  if (symbol_entries > kMaxSymbolEntries - symbol_entries_read) {
    return absl::InternalError(
        absl::StrCat("too many symbols: ", symbol_entries));
  }
  symbol_entries_read += symbol_entries;
  if (symtab.sh_link >= section_headers_.size()) {
    return absl::InternalError(
        absl::StrCat("invalid symtab's strtab reference: ", symtab.sh_link));
  }
  SAPI_RAW_VLOG(1, "Symbol table with %zu entries found", symbol_entries);
  SAPI_ASSIGN_OR_RETURN(std::string strtab,
                        ReadSectionContents(symtab.sh_link));
  SAPI_ASSIGN_OR_RETURN(std::string symbols, ReadSectionContents(symtab));
  result_.symbols_.reserve(result_.symbols_.size() + symbol_entries);
  for (absl::string_view src = symbols; !src.empty();
       src = src.substr(symtab.sh_entsize)) {
    ElfSym symbol;
    LOAD_MEMBER(symbol, st_name, src.data());
    LOAD_MEMBER(symbol, st_info, src.data());
    LOAD_MEMBER(symbol, st_other, src.data());
    LOAD_MEMBER(symbol, st_shndx, src.data());
    LOAD_MEMBER(symbol, st_value, src.data());
    LOAD_MEMBER(symbol, st_size, src.data());
    if (symbol.st_shndx == SHN_UNDEF) {
      // External symbol, not supported.
      continue;
    }
    if (symbol.st_shndx == SHN_ABS) {
      // Absolute value, not supported.
      continue;
    }
    if (symbol.st_shndx >= section_headers_.size()) {
      return absl::FailedPreconditionError(absl::StrCat(
          "invalid symbol data: section index: ", symbol.st_shndx));
    }
    if (symbol.st_name >= strtab.size()) {
      return absl::FailedPreconditionError(
          absl::StrCat("invalid name reference: REL", symbol.st_value));
    }
    result_.symbols_.push_back(
        {symbol.st_value, std::string(ReadName(symbol.st_name, strtab))});
  }
  return absl::OkStatus();
}

absl::Status ElfParser::ReadImportedLibrariesFromDynamic(
    const ElfShdr& dynamic) {
  if (dynamic.sh_type != SHT_DYNAMIC) {
    return absl::FailedPreconditionError("invalid dynamic type");
  }
  if (dynamic.sh_entsize != sizeof(ElfDyn)) {
    return absl::InternalError(
        absl::StrCat("invalid dynamic entry size: ", dynamic.sh_entsize));
  }
  if ((dynamic.sh_size % dynamic.sh_entsize) != 0) {
    return absl::InternalError(
        absl::StrCat("invalid dynamic table size: ", dynamic.sh_size));
  }
  size_t entries = dynamic.sh_size / dynamic.sh_entsize;
  if (entries > kMaxDynamicEntries - dynamic_entries_read) {
    return absl::InternalError(
        absl::StrCat("too many dynamic entries: ", entries));
  }
  dynamic_entries_read += entries;
  if (dynamic.sh_link >= section_headers_.size()) {
    return absl::InternalError(
        absl::StrCat("invalid dynamic's strtab reference: ", dynamic.sh_link));
  }
  SAPI_RAW_VLOG(1, "Dynamic section with %zu entries found", entries);
  // strtab may be shared with symbols and therefore huge
  const auto& strtab_section = section_headers_.at(dynamic.sh_link);
  if (strtab_section.sh_offset > file_size_) {
    return absl::FailedPreconditionError(absl::StrCat(
        "invalid symtab's strtab section offset: ", strtab_section.sh_offset));
  }
  if (strtab_section.sh_size >= kMaxStrtabSize ||
      strtab_section.sh_size >= file_size_ ||
      strtab_section.sh_offset >= file_size_ - strtab_section.sh_size) {
    return absl::FailedPreconditionError(
        absl::StrCat("symtab's strtab too big: ", strtab_section.sh_size));
  }
  auto strtab_end = strtab_section.sh_offset + strtab_section.sh_size;
  SAPI_ASSIGN_OR_RETURN(std::string dynamic_entries,
                        ReadSectionContents(dynamic));
  for (absl::string_view src = dynamic_entries; !src.empty();
       src = src.substr(dynamic.sh_entsize)) {
    ElfDyn dyn;
    LOAD_MEMBER(dyn, d_tag, src.data());
    LOAD_MEMBER(dyn, d_un.d_val, src.data());
    if (dyn.d_tag != DT_NEEDED) {
      continue;
    }
    if (dyn.d_un.d_val >= strtab_section.sh_size) {
      return absl::FailedPreconditionError(
          absl::StrCat("invalid name reference"));
    }
    auto offset = strtab_section.sh_offset + dyn.d_un.d_val;
    SAPI_RETURN_IF_ERROR(CheckedFSeek(elf_, offset, SEEK_SET));
    std::string path(
        std::min(kMaxLibPathSize, static_cast<size_t>(strtab_end - offset)),
        '\0');
    size_t size = std::fread(&path[0], 1, path.size(), elf_);
    path.resize(size);
    result_.imported_libraries_.push_back(path.substr(0, path.find('\0')));
  }
  return absl::OkStatus();
}

absl::StatusOr<ElfFile> ElfParser::Parse(const std::string& filename,
                                         uint32_t features) {
  ElfParser parser;
  if (parser.elf_ = std::fopen(filename.c_str(), "r"); !parser.elf_) {
    return absl::ErrnoToStatus(errno,
                               absl::StrCat("cannot open file: ", filename));
  }

  // Basic sanity check.
  if (features & ~(ElfFile::kAll)) {
    return absl::InvalidArgumentError("Unknown feature flags specified");
  }
  SAPI_RETURN_IF_ERROR(parser.ReadFileSize());
  SAPI_RETURN_IF_ERROR(parser.ReadFileHeader());
  switch (parser.file_header_.e_type) {
    case ET_EXEC:
      parser.result_.position_independent_ = false;
      break;
    case ET_DYN:
      parser.result_.position_independent_ = true;
      break;
    default:
      return absl::FailedPreconditionError("not an executable: ");
  }
  if (features & ElfFile::kGetInterpreter) {
    SAPI_RETURN_IF_ERROR(parser.ReadProgramHeaders());
    std::string interpreter;
    auto it = std::find_if(
        parser.program_headers_.begin(), parser.program_headers_.end(),
        [](const ElfPhdr& hdr) { return hdr.p_type == PT_INTERP; });
    // No interpreter usually means that the executable was statically linked.
    if (it != parser.program_headers_.end()) {
      if (it->p_filesz > kMaxInterpreterSize) {
        return absl::FailedPreconditionError(
            absl::StrCat("program interpeter path too long: ", it->p_filesz));
      }
      SAPI_RETURN_IF_ERROR(CheckedFSeek(parser.elf_, it->p_offset, SEEK_SET));
      interpreter.resize(it->p_filesz, '\0');
      SAPI_RETURN_IF_ERROR(CheckedRead(&interpreter, parser.elf_));
      auto first_nul = interpreter.find_first_of('\0');
      if (first_nul != std::string::npos) {
        interpreter.erase(first_nul);
      }
    }
    parser.result_.interpreter_ = std::move(interpreter);
  }

  if (features & (ElfFile::kLoadSymbols | ElfFile::kLoadImportedLibraries)) {
    SAPI_RETURN_IF_ERROR(parser.ReadSectionHeaders());
    for (const auto& hdr : parser.section_headers_) {
      if (hdr.sh_type == SHT_SYMTAB && features & ElfFile::kLoadSymbols) {
        SAPI_RETURN_IF_ERROR(parser.ReadSymbolsFromSymtab(hdr));
      }
      if (hdr.sh_type == SHT_DYNAMIC &&
          features & ElfFile::kLoadImportedLibraries) {
        SAPI_RETURN_IF_ERROR(parser.ReadImportedLibrariesFromDynamic(hdr));
      }
    }
  }

  return std::move(parser.result_);
}

absl::StatusOr<ElfFile> ElfFile::ParseFromFile(const std::string& filename,
                                               uint32_t features) {
  return ElfParser::Parse(filename, features);
}

}  // namespace sandbox2
