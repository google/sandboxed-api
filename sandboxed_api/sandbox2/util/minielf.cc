// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
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

#include "absl/base/internal/endian.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/sandbox2/util.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {

constexpr int kElfHeaderSize =
    sizeof(Elf64_Ehdr);  // Maximum size for 64-bit binaries

constexpr char kElfMagic[] =
    "\x7F"
    "ELF";

constexpr int kEiClassOffset = 0x04;
constexpr int kEiClass64 = 2;  // 64-bit binary

constexpr int kEiDataOffset = 0x05;
constexpr int kEiDataLittle = 1;  // Little Endian
constexpr int kEiDataBig = 2;     // Big Endian

constexpr int kEiVersionOffset = 0x06;
constexpr int kEvCurrent = 1;  // ELF version

namespace {

// NOLINTNEXTLINE
absl::Status CheckedFSeek(FILE* f, long offset, int whence) {
  if (fseek(f, offset, whence)) {
    return absl::FailedPreconditionError(
        absl::StrCat("Fseek on ELF failed: ", StrError(errno)));
  }
  return absl::OkStatus();
}

absl::Status CheckedFRead(void* dst, size_t size, size_t nmemb, FILE* f) {
  if (fread(dst, size, nmemb, f) == nmemb) {
    return absl::OkStatus();
  }
  return absl::FailedPreconditionError(
      absl::StrCat("Reading ELF data failed: ", StrError(errno)));
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

  ElfParser() = default;
  sapi::StatusOr<ElfFile> Parse(FILE* elf, uint32_t features);

 private:
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
  void Load(uint8_t* dst, const void* src) {
    *dst = *reinterpret_cast<const char*>(src);
  }
  void Load(uint16_t* dst, const void* src) { *dst = Load16(src); }
  void Load(uint32_t* dst, const void* src) { *dst = Load32(src); }
  void Load(uint64_t* dst, const void* src) { *dst = Load64(src); }
  void Load(int8_t* dst, const void* src) {
    *dst = *reinterpret_cast<const char*>(src);
  }
  void Load(int16_t* dst, const void* src) { *dst = Load16(src); }
  void Load(int32_t* dst, const void* src) { *dst = Load32(src); }
  void Load(int64_t* dst, const void* src) { *dst = Load64(src); }

  // Reads elf file size.
  absl::Status ReadFileSize();
  // Reads elf header.
  absl::Status ReadFileHeader();
  // Reads a single elf program header.
  sapi::StatusOr<Elf64_Phdr> ReadProgramHeader(absl::string_view src);
  // Reads all elf program headers.
  absl::Status ReadProgramHeaders();
  // Reads a single elf section header.
  sapi::StatusOr<Elf64_Shdr> ReadSectionHeader(absl::string_view src);
  // Reads all elf section headers.
  absl::Status ReadSectionHeaders();
  // Reads contents of an elf section.
  sapi::StatusOr<std::string> ReadSectionContents(int idx);
  sapi::StatusOr<std::string> ReadSectionContents(
      const Elf64_Shdr& section_header);
  // Reads all symbols from symtab section.
  absl::Status ReadSymbolsFromSymtab(const Elf64_Shdr& symtab);
  // Reads all imported libraries from dynamic section.
  absl::Status ReadImportedLibrariesFromDynamic(const Elf64_Shdr& dynamic);

  ElfFile result_;
  FILE* elf_ = nullptr;
  size_t file_size_ = 0;
  bool elf_little_ = false;
  Elf64_Ehdr file_header_;
  std::vector<Elf64_Phdr> program_headers_;
  std::vector<Elf64_Shdr> section_headers_;

  int symbol_entries_read = 0;
  int dynamic_entries_read = 0;
};

constexpr int ElfParser::kMaxProgramHeaderEntries;
constexpr int ElfParser::kMaxSectionHeaderEntries;
constexpr size_t ElfParser::kMaxSectionSize;
constexpr size_t ElfParser::kMaxStrtabSize;
constexpr size_t ElfParser::kMaxLibPathSize;
constexpr int ElfParser::kMaxSymbolEntries;
constexpr int ElfParser::kMaxDynamicEntries;
constexpr size_t ElfParser::kMaxInterpreterSize;

absl::Status ElfParser::ReadFileSize() {
  fseek(elf_, 0, SEEK_END);
  file_size_ = ftell(elf_);
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

  if (header[kEiClassOffset] != kEiClass64) {
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

sapi::StatusOr<Elf64_Shdr> ElfParser::ReadSectionHeader(
    absl::string_view src) {
  if (src.size() < sizeof(Elf64_Shdr)) {
    return absl::FailedPreconditionError(
        absl::StrCat("invalid section header data: got ", src.size(),
                     " bytes, ", sizeof(Elf64_Shdr), " bytes expected."));
  }
  Elf64_Shdr rv;
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
  if (file_header_.e_shentsize != sizeof(Elf64_Shdr)) {
    return absl::FailedPreconditionError(absl::StrCat(
        "section header entry size incorrect: ", file_header_.e_shentsize,
        " bytes, ", sizeof(Elf64_Shdr), " expected."));
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

sapi::StatusOr<std::string> ElfParser::ReadSectionContents(int idx) {
  if (idx < 0 || idx >= section_headers_.size()) {
    return absl::FailedPreconditionError(
        absl::StrCat("invalid section header index: ", idx));
  }
  return ReadSectionContents(section_headers_.at(idx));
}

sapi::StatusOr<std::string> ElfParser::ReadSectionContents(
    const Elf64_Shdr& section_header) {
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

sapi::StatusOr<Elf64_Phdr> ElfParser::ReadProgramHeader(
    absl::string_view src) {
  if (src.size() < sizeof(Elf64_Phdr)) {
    return absl::FailedPreconditionError(
        absl::StrCat("invalid program header data: got ", src.size(),
                     " bytes, ", sizeof(Elf64_Phdr), " bytes expected."));
  }
  Elf64_Phdr rv;
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
  if (file_header_.e_phentsize != sizeof(Elf64_Phdr)) {
    return absl::FailedPreconditionError(absl::StrCat(
        "section header entry size incorrect: ", file_header_.e_phentsize,
        " bytes, ", sizeof(Elf64_Phdr), " expected."));
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

absl::Status ElfParser::ReadSymbolsFromSymtab(const Elf64_Shdr& symtab) {
  if (symtab.sh_type != SHT_SYMTAB) {
    return absl::FailedPreconditionError("invalid symtab type");
  }
  if (symtab.sh_entsize != sizeof(Elf64_Sym)) {
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
  SAPI_RAW_VLOG(1, "Symbol table with %d entries found", symbol_entries);
  SAPI_ASSIGN_OR_RETURN(std::string strtab, ReadSectionContents(symtab.sh_link));
  SAPI_ASSIGN_OR_RETURN(std::string symbols, ReadSectionContents(symtab));
  result_.symbols_.reserve(result_.symbols_.size() + symbol_entries);
  for (absl::string_view src = symbols; !src.empty();
       src = src.substr(symtab.sh_entsize)) {
    Elf64_Sym symbol;
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
    const Elf64_Shdr& dynamic) {
  if (dynamic.sh_type != SHT_DYNAMIC) {
    return absl::FailedPreconditionError("invalid dynamic type");
  }
  if (dynamic.sh_entsize != sizeof(Elf64_Dyn)) {
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
  SAPI_RAW_VLOG(1, "Dynamic section with %d entries found", entries);
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
  SAPI_ASSIGN_OR_RETURN(std::string dynamic_entries, ReadSectionContents(dynamic));
  for (absl::string_view src = dynamic_entries; !src.empty();
       src = src.substr(dynamic.sh_entsize)) {
    Elf64_Dyn dyn;
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
    std::string path(std::min(kMaxLibPathSize, strtab_end - offset), '\0');
    size_t size = fread(&path[0], 1, path.size(), elf_);
    path.resize(size);
    result_.imported_libraries_.push_back(path.substr(0, path.find('\0')));
  }
  return absl::OkStatus();
}

sapi::StatusOr<ElfFile> ElfParser::Parse(FILE* elf, uint32_t features) {
  elf_ = elf;
  // Basic sanity check.
  if (features & ~(ElfFile::kAll)) {
    return absl::InvalidArgumentError("Unknown feature flags specified");
  }
  SAPI_RETURN_IF_ERROR(ReadFileSize());
  SAPI_RETURN_IF_ERROR(ReadFileHeader());
  switch (file_header_.e_type) {
    case ET_EXEC:
      result_.position_independent_ = false;
      break;
    case ET_DYN:
      result_.position_independent_ = true;
      break;
    default:
      return absl::FailedPreconditionError("not an executable: ");
  }
  if (features & ElfFile::kGetInterpreter) {
    SAPI_RETURN_IF_ERROR(ReadProgramHeaders());
    std::string interpreter;
    auto it = std::find_if(
        program_headers_.begin(), program_headers_.end(),
        [](const Elf64_Phdr& hdr) { return hdr.p_type == PT_INTERP; });
    // No interpreter usually means that the executable was statically linked.
    if (it != program_headers_.end()) {
      if (it->p_filesz > kMaxInterpreterSize) {
        return absl::FailedPreconditionError(
            absl::StrCat("program interpeter path too long: ", it->p_filesz));
      }
      SAPI_RETURN_IF_ERROR(CheckedFSeek(elf, it->p_offset, SEEK_SET));
      interpreter.resize(it->p_filesz, '\0');
      SAPI_RETURN_IF_ERROR(CheckedRead(&interpreter, elf));
      auto first_nul = interpreter.find_first_of('\0');
      if (first_nul != std::string::npos) {
        interpreter.erase(first_nul);
      }
    }
    result_.interpreter_ = std::move(interpreter);
  }

  if (features & (ElfFile::kLoadSymbols | ElfFile::kLoadImportedLibraries)) {
    SAPI_RETURN_IF_ERROR(ReadSectionHeaders());
    for (const auto& hdr : section_headers_) {
      if (hdr.sh_type == SHT_SYMTAB && features & ElfFile::kLoadSymbols) {
        SAPI_RETURN_IF_ERROR(ReadSymbolsFromSymtab(hdr));
      }
      if (hdr.sh_type == SHT_DYNAMIC &&
          features & ElfFile::kLoadImportedLibraries) {
        SAPI_RETURN_IF_ERROR(ReadImportedLibrariesFromDynamic(hdr));
      }
    }
  }

  return std::move(result_);
}

sapi::StatusOr<ElfFile> ElfFile::ParseFromFile(const std::string& filename,
                                                 uint32_t features) {
  std::unique_ptr<FILE, void (*)(FILE*)> elf{fopen(filename.c_str(), "r"),
                                             [](FILE* f) { fclose(f); }};
  if (!elf) {
    return absl::UnknownError(
        absl::StrCat("cannot open file: ", filename, ": ", StrError(errno)));
  }

  return ElfParser().Parse(elf.get(), features);
}

}  // namespace sandbox2
