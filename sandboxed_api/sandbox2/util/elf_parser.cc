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

#include "sandboxed_api/sandbox2/util/elf_parser.h"

#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/numeric/bits.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/config.h"
#include "sandboxed_api/util/raw_logging.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2 {

namespace {

//  Arbitrary cut-off values, so we can parse safely.
constexpr int kMaxProgramHeaderEntries = 500;
constexpr int kMaxSectionHeaderEntries = 500;
constexpr size_t kMaxDataSize = 500 * 1024 * 1024;
constexpr int kMaxSymbolEntries = 4 * 1000 * 1000;
constexpr int kMaxDynamicEntries = 10000;
constexpr size_t kMaxInterpreterSize = 1000;

absl::string_view ReadString(uint32_t offset, absl::string_view strtab) {
  absl::string_view str = strtab.substr(offset);
  return str.substr(0, str.find('\0'));
}

}  //  namespace

#define LOAD_MEMBER(data_struct, member, src)                            \
  Load(&(data_struct).member,                                            \
       &src[offsetof(std::remove_reference<decltype(data_struct)>::type, \
                     member)])

absl::StatusOr<std::unique_ptr<ElfParser>> ElfParser::Create(
    absl::string_view filename, bool mmap_file) {
  std::unique_ptr<ElfParser> parser(new ElfParser());
  SAPI_RETURN_IF_ERROR(parser->Init(filename, mmap_file));
  return parser;
}

absl::Status ElfParser::Init(absl::string_view filename, bool mmap_file) {
  fd_ = open(std::string(filename).c_str(), O_RDONLY);
  if (fd_ == -1) {
    return absl::ErrnoToStatus(errno,
                               absl::StrCat("failed to open: ", filename));
  }
  struct stat statbuf;
  if (fstat(fd_, &statbuf)) {
    return absl::ErrnoToStatus(errno,
                               absl::StrCat("failed to stat: ", filename));
  }
  if (mmap_file) {
    const char* data = static_cast<const char*>(
        mmap(nullptr, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd_, 0));
    if (data == MAP_FAILED) {
      return absl::ErrnoToStatus(errno,
                                 absl::StrCat("failed to mmap: ", filename));
    }
    mmap_ = absl::string_view(data, statbuf.st_size);
  }
  return ReadFileHeader();
}

ElfParser::~ElfParser() {
  if (fd_ != -1) {
    close(fd_);
  }
  if (mmap_.data()) {
    munmap(const_cast<char*>(mmap_.data()), mmap_.size());
  }
}

absl::StatusOr<ElfParser::Buffer> ElfParser::ReadData(size_t offset,
                                                      size_t size) {
  if (size > kMaxDataSize) {
    return absl::InvalidArgumentError(absl::StrCat(
        "too big data read (likely too large ELF section): size: ", size,
        " max size: ", kMaxDataSize));
  }
  if (mmap_.data()) {
    if (offset >= mmap_.size() || size > mmap_.size() ||
        offset + size > mmap_.size()) {
      return absl::InvalidArgumentError(
          absl::StrCat("invalid data read: offset: ", offset, " size: ", size,
                       "file size: ", mmap_.size()));
    }
    return Buffer{mmap_.substr(offset, size), std::string()};
  }
  if (lseek(fd_, offset, SEEK_SET) == -1) {
    return absl::ErrnoToStatus(errno, absl::StrCat("failed to lseek"));
  }
  std::string buffer(size, '\0');
  for (size_t read_bytes = 0; read_bytes != size;) {
    size_t n = read(fd_, buffer.data() + read_bytes, size - read_bytes);
    if (n == -1) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      return absl::ErrnoToStatus(errno, absl::StrCat("failed to read"));
    }
    if (n == 0) {
      return absl::OutOfRangeError(absl::StrCat("failed to read (EOF)"));
    }
    read_bytes += n;
  }
  absl::string_view data(buffer);
  return Buffer{data, std::move(buffer)};
}

absl::Status ElfParser::ReadFileHeader() {
  SAPI_ASSIGN_OR_RETURN(Buffer header_buf, ReadData(0, sizeof(ElfEhdr)));
  absl::string_view header = header_buf.data;
  if (!absl::StartsWith(header, ELFMAG)) {
    return absl::FailedPreconditionError("magic not found, not an ELF");
  }

  constexpr int kEiClassOffset = 0x04;
  constexpr int kEiClass = host_cpu::Is64Bit() ? ELFCLASS64 : ELFCLASS32;
  if (header[kEiClassOffset] != kEiClass) {
    return absl::FailedPreconditionError("invalid ELF class");
  }

  constexpr int kEiDataOffset = 0x05;
  constexpr int kEiDataLittle = 1;  // Little Endian
  constexpr int kEiDataBig = 2;     // Big Endian
  const char elf_data = header[kEiDataOffset];
  elf_little_ = elf_data == kEiDataLittle;
  if (!elf_little_ && elf_data != kEiDataBig) {
    return absl::FailedPreconditionError("invalid endianness");
  }

  constexpr int kEiVersionOffset = 0x06;
  constexpr int kEvCurrent = 1;  // ELF version
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

absl::Status ElfParser::ForEachProgram(
    absl::FunctionRef<absl::Status(const ElfPhdr&)> callback) {
  SAPI_RETURN_IF_ERROR(ReadProgramHeaders());
  for (const auto& hdr : program_headers_) {
    SAPI_RETURN_IF_ERROR(callback(hdr));
  }
  return absl::OkStatus();
}

absl::Status ElfParser::ForEachSection(
    absl::FunctionRef<absl::Status(const ElfShdr&)> callback) {
  SAPI_RETURN_IF_ERROR(ReadSectionHeaders());
  for (const auto& hdr : section_headers_) {
    SAPI_RETURN_IF_ERROR(callback(hdr));
  }
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
  if (!section_headers_.empty()) {
    return absl::OkStatus();
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
  SAPI_ASSIGN_OR_RETURN(
      Buffer headers, ReadData(file_header_.e_shoff, file_header_.e_shentsize *
                                                         file_header_.e_shnum));
  std::vector<ElfShdr> tmp(file_header_.e_shnum);
  absl::string_view src = headers.data;
  for (int i = 0; i < file_header_.e_shnum; ++i) {
    SAPI_ASSIGN_OR_RETURN(tmp[i], ReadSectionHeader(src));
    src = src.substr(file_header_.e_shentsize);
  }
  section_headers_.swap(tmp);
  return absl::OkStatus();
}

absl::StatusOr<ElfParser::Buffer> ElfParser::ReadSectionContents(int idx) {
  if (idx < 0 || idx >= section_headers_.size()) {
    return absl::FailedPreconditionError(
        absl::StrCat("invalid section header index: ", idx));
  }
  return ReadSectionContents(section_headers_.at(idx));
}

absl::StatusOr<ElfParser::Buffer> ElfParser::ReadSectionContents(
    const ElfShdr& section_header) {
  return ReadData(section_header.sh_offset, section_header.sh_size);
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
  if (!program_headers_.empty()) {
    return absl::OkStatus();
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
  SAPI_ASSIGN_OR_RETURN(
      Buffer headers, ReadData(file_header_.e_phoff, file_header_.e_phentsize *
                                                         file_header_.e_phnum));
  std::vector<ElfPhdr> tmp(file_header_.e_phnum);
  absl::string_view src = headers.data;
  for (int i = 0; i < file_header_.e_phnum; ++i) {
    SAPI_ASSIGN_OR_RETURN(tmp[i], ReadProgramHeader(src));
    src = src.substr(file_header_.e_phentsize);
  }
  program_headers_.swap(tmp);
  return absl::OkStatus();
}

absl::Status ElfParser::ReadSymbolsFromSymtab(
    const ElfShdr& symtab,
    absl::FunctionRef<void(uintptr_t, absl::string_view)> symbol_callback) {
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
  if (symbol_entries > kMaxSymbolEntries - symbol_entries_read_) {
    return absl::InternalError(
        absl::StrCat("too many symbols: ", symbol_entries));
  }
  symbol_entries_read_ += symbol_entries;
  if (symtab.sh_link >= section_headers_.size()) {
    return absl::InternalError(
        absl::StrCat("invalid symtab's strtab reference: ", symtab.sh_link));
  }
  SAPI_RAW_VLOG(1, "Symbol table with %zu entries found", symbol_entries);
  SAPI_ASSIGN_OR_RETURN(Buffer strtab, ReadSectionContents(symtab.sh_link));
  SAPI_ASSIGN_OR_RETURN(Buffer symbols, ReadSectionContents(symtab));
  for (absl::string_view src = symbols.data; !src.empty();
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
    if (symbol.st_name >= strtab.data.size()) {
      return absl::FailedPreconditionError(
          absl::StrCat("invalid name reference: REL", symbol.st_value));
    }
    symbol_callback(symbol.st_value, ReadString(symbol.st_name, strtab.data));
  }
  return absl::OkStatus();
}

absl::Status ElfParser::ReadImportedLibrariesFromDynamic(
    const ElfShdr& dynamic,
    absl::FunctionRef<void(absl::string_view)> library_callback) {
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
  if (entries > kMaxDynamicEntries - dynamic_entries_read_) {
    return absl::InternalError(
        absl::StrCat("too many dynamic entries: ", entries));
  }
  dynamic_entries_read_ += entries;
  if (dynamic.sh_link >= section_headers_.size()) {
    return absl::InternalError(
        absl::StrCat("invalid dynamic's strtab reference: ", dynamic.sh_link));
  }
  SAPI_RAW_VLOG(1, "Dynamic section with %zu entries found", entries);
  // strtab may be shared with symbols and therefore huge
  const auto& strtab_section = section_headers_.at(dynamic.sh_link);
  SAPI_ASSIGN_OR_RETURN(Buffer strtab, ReadSectionContents(strtab_section));
  SAPI_ASSIGN_OR_RETURN(Buffer dynamic_entries, ReadSectionContents(dynamic));
  for (absl::string_view src = dynamic_entries.data; !src.empty();
       src = src.substr(dynamic.sh_entsize)) {
    ElfDyn dyn;
    LOAD_MEMBER(dyn, d_tag, src.data());
    LOAD_MEMBER(dyn, d_un.d_val, src.data());
    if (dyn.d_tag != DT_NEEDED) {
      continue;
    }
    if (dyn.d_un.d_val >= strtab.data.size()) {
      return absl::FailedPreconditionError(
          absl::StrCat("invalid name reference"));
    }
    library_callback(ReadString(dyn.d_un.d_val, strtab.data));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> ElfParser::ReadInterpreter() {
  SAPI_RETURN_IF_ERROR(ReadProgramHeaders());
  auto it =
      std::find_if(program_headers_.begin(), program_headers_.end(),
                   [](const ElfPhdr& hdr) { return hdr.p_type == PT_INTERP; });
  // No interpreter usually means that the executable was statically linked.
  if (it == program_headers_.end()) {
    return "";
  }
  if (it->p_filesz > kMaxInterpreterSize) {
    return absl::FailedPreconditionError(
        absl::StrCat("program interpreter path too long: ", it->p_filesz));
  }
  SAPI_ASSIGN_OR_RETURN(Buffer interpreter,
                        ReadData(it->p_offset, it->p_filesz));
  return std::string(ReadString(0, interpreter.data));
}

uint16_t ElfParser::Load16(const void* src) {
  uint16_t v;
  memcpy(&v, src, sizeof(v));
  if constexpr (absl::endian::native == absl::endian::little) {
    return elf_little_ ? v : absl::byteswap(v);
  }
  return elf_little_ ? absl::byteswap(v) : v;
}

uint32_t ElfParser::Load32(const void* src) {
  uint32_t v;
  memcpy(&v, src, sizeof(v));
  if constexpr (absl::endian::native == absl::endian::little) {
    return elf_little_ ? v : absl::byteswap(v);
  }
  return elf_little_ ? absl::byteswap(v) : v;
}

uint64_t ElfParser::Load64(const void* src) {
  uint64_t v;
  memcpy(&v, src, sizeof(v));
  if constexpr (absl::endian::native == absl::endian::little) {
    return elf_little_ ? v : absl::byteswap(v);
  }
  return elf_little_ ? absl::byteswap(v) : v;
}

template <size_t N>
void ElfParser::Load(unsigned char (*dst)[N], const void* src) {
  // TODO(cblichmann): add a test for this.
  memcpy(dst, src, N);
}

template <typename IntT>
std::enable_if_t<std::is_integral_v<IntT>, void> ElfParser::Load(
    IntT* dst, const void* src) {
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

}  // namespace sandbox2
