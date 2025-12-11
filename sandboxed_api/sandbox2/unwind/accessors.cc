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

#include "sandboxed_api/sandbox2/unwind/accessors.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "libunwind.h"
#include "sandboxed_api/sandbox2/regs.h"
#include "sandboxed_api/sandbox2/unwind/accessors_internal.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {

namespace {

class MMappedWrapper {
 public:
  static absl::StatusOr<MMappedWrapper> MapFile(const char* path) {
    struct stat stat;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return absl::ErrnoToStatus(errno, "open failed");

    sapi::file_util::fileops::FDCloser mapped_fd(fd);
    if (fstat(fd, &stat) < 0) {
      return absl::ErrnoToStatus(errno, "fstat failed");
    }

    size_t size = stat.st_size;
    void* data = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
      return absl::ErrnoToStatus(errno, "mmap failed");
    }
    return MMappedWrapper(data, size);
  }

  MMappedWrapper() = default;
  MMappedWrapper(const MMappedWrapper&) = delete;
  MMappedWrapper& operator=(const MMappedWrapper&) = delete;
  MMappedWrapper(MMappedWrapper&& other) { *this = std::move(other); }
  MMappedWrapper& operator=(MMappedWrapper&& other) {
    if (this == &other) {
      return *this;
    }
    Reset();
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
    return *this;
  }
  void Reset() {
    if (data_ != nullptr) {
      if (munmap(data_, size_) < 0) {
        SAPI_RAW_PLOG(ERROR, "munmap failed");
      }
    }
    data_ = nullptr;
    size_ = 0;
  }
  ~MMappedWrapper() { Reset(); }

  void* data() const { return data_; }
  size_t size() const { return size_; }

 private:
  MMappedWrapper(void* data, size_t size) : data_(data), size_(size) {}

  void* data_ = nullptr;
  size_t size_ = 0;
};

unw_word_t GetReg(const sandbox2::Regs::PtraceRegisters& regs,
                  unw_regnum_t reg) {
  switch (reg) {
#if defined(SAPI_X86_64)
    case UNW_X86_64_RAX:
      return regs.rax;
    case UNW_X86_64_RDX:
      return regs.rdx;
    case UNW_X86_64_RCX:
      return regs.rcx;
    case UNW_X86_64_RBX:
      return regs.rbx;
    case UNW_X86_64_RSI:
      return regs.rsi;
    case UNW_X86_64_RDI:
      return regs.rdi;
    case UNW_X86_64_RBP:
      return regs.rbp;
    case UNW_X86_64_RSP:
      return regs.rsp;
    case UNW_X86_64_R8:
      return regs.r8;
    case UNW_X86_64_R9:
      return regs.r9;
    case UNW_X86_64_R10:
      return regs.r10;
    case UNW_X86_64_R11:
      return regs.r11;
    case UNW_X86_64_R12:
      return regs.r12;
    case UNW_X86_64_R13:
      return regs.r13;
    case UNW_X86_64_R14:
      return regs.r14;
    case UNW_X86_64_R15:
      return regs.r15;
    case UNW_X86_64_RIP:
      return regs.rip;
#elif defined(SAPI_PPC64_LE)
    case UNW_PPC64_R0:
      return regs.gpr[0];
    case UNW_PPC64_R1:
      return regs.gpr[1];
    case UNW_PPC64_R2:
      return regs.gpr[2];
    case UNW_PPC64_R3:
      return regs.gpr[3];
    case UNW_PPC64_R4:
      return regs.gpr[4];
    case UNW_PPC64_R5:
      return regs.gpr[5];
    case UNW_PPC64_R6:
      return regs.gpr[6];
    case UNW_PPC64_R7:
      return regs.gpr[7];
    case UNW_PPC64_R8:
      return regs.gpr[8];
    case UNW_PPC64_R9:
      return regs.gpr[9];
    case UNW_PPC64_R10:
      return regs.gpr[10];
    case UNW_PPC64_R11:
      return regs.gpr[11];
    case UNW_PPC64_R12:
      return regs.gpr[12];
    case UNW_PPC64_R13:
      return regs.gpr[13];
    case UNW_PPC64_R14:
      return regs.gpr[14];
    case UNW_PPC64_R15:
      return regs.gpr[15];
    case UNW_PPC64_R16:
      return regs.gpr[16];
    case UNW_PPC64_R17:
      return regs.gpr[17];
    case UNW_PPC64_R18:
      return regs.gpr[18];
    case UNW_PPC64_R19:
      return regs.gpr[19];
    case UNW_PPC64_R20:
      return regs.gpr[20];
    case UNW_PPC64_R21:
      return regs.gpr[21];
    case UNW_PPC64_R22:
      return regs.gpr[22];
    case UNW_PPC64_R23:
      return regs.gpr[23];
    case UNW_PPC64_R24:
      return regs.gpr[24];
    case UNW_PPC64_R25:
      return regs.gpr[25];
    case UNW_PPC64_R26:
      return regs.gpr[26];
    case UNW_PPC64_R27:
      return regs.gpr[27];
    case UNW_PPC64_R28:
      return regs.gpr[28];
    case UNW_PPC64_R29:
      return regs.gpr[29];
    case UNW_PPC64_R30:
      return regs.gpr[30];
    case UNW_PPC64_R31:
      return regs.gpr[31];
    case UNW_PPC64_NIP:
      return regs.nip;
    case UNW_PPC64_CTR:
      return regs.ctr;
    case UNW_PPC64_XER:
      return regs.xer;
    case UNW_PPC64_LR:
      return regs.link;
#elif defined(SAPI_ARM64)
    case UNW_AARCH64_X0:
      return regs.regs[0];
    case UNW_AARCH64_X1:
      return regs.regs[1];
    case UNW_AARCH64_X2:
      return regs.regs[2];
    case UNW_AARCH64_X3:
      return regs.regs[3];
    case UNW_AARCH64_X4:
      return regs.regs[4];
    case UNW_AARCH64_X5:
      return regs.regs[5];
    case UNW_AARCH64_X6:
      return regs.regs[6];
    case UNW_AARCH64_X7:
      return regs.regs[7];
    case UNW_AARCH64_X8:
      return regs.regs[8];
    case UNW_AARCH64_X9:
      return regs.regs[9];
    case UNW_AARCH64_X10:
      return regs.regs[10];
    case UNW_AARCH64_X11:
      return regs.regs[11];
    case UNW_AARCH64_X12:
      return regs.regs[12];
    case UNW_AARCH64_X13:
      return regs.regs[13];
    case UNW_AARCH64_X14:
      return regs.regs[14];
    case UNW_AARCH64_X15:
      return regs.regs[15];
    case UNW_AARCH64_X16:
      return regs.regs[16];
    case UNW_AARCH64_X17:
      return regs.regs[17];
    case UNW_AARCH64_X18:
      return regs.regs[18];
    case UNW_AARCH64_X19:
      return regs.regs[19];
    case UNW_AARCH64_X20:
      return regs.regs[20];
    case UNW_AARCH64_X21:
      return regs.regs[21];
    case UNW_AARCH64_X22:
      return regs.regs[22];
    case UNW_AARCH64_X23:
      return regs.regs[23];
    case UNW_AARCH64_X24:
      return regs.regs[24];
    case UNW_AARCH64_X25:
      return regs.regs[25];
    case UNW_AARCH64_X26:
      return regs.regs[26];
    case UNW_AARCH64_X27:
      return regs.regs[27];
    case UNW_AARCH64_X28:
      return regs.regs[28];
    case UNW_AARCH64_X29:
      return regs.regs[29];
    case UNW_AARCH64_X30:
      return regs.regs[30];
    case UNW_AARCH64_SP:
      return regs.sp;
    case UNW_AARCH64_PC:
      return regs.pc;
    case UNW_AARCH64_PSTATE:
      return regs.pstate;
#elif defined(SAPI_ARM)
    case UNW_ARM_R0:
      return regs.regs[0];
    case UNW_ARM_R1:
      return regs.regs[1];
    case UNW_ARM_R2:
      return regs.regs[2];
    case UNW_ARM_R3:
      return regs.regs[3];
    case UNW_ARM_R4:
      return regs.regs[4];
    case UNW_ARM_R5:
      return regs.regs[5];
    case UNW_ARM_R6:
      return regs.regs[6];
    case UNW_ARM_R7:
      return regs.regs[7];
    case UNW_ARM_R8:
      return regs.regs[8];
    case UNW_ARM_R9:
      return regs.regs[9];
    case UNW_ARM_R10:
      return regs.regs[10];
    case UNW_ARM_R11:
      return regs.regs[11];
    case UNW_ARM_R12:
      return regs.regs[12];
    case UNW_ARM_R13:
      return regs.regs[13];
    case UNW_ARM_R14:
      return regs.regs[14];
    case UNW_ARM_R15:
      return regs.pc;
#endif
  }
  SAPI_RAW_LOG(FATAL, "Unsupported register: %d", reg);
}

int FindProcInfo(unw_addr_space_t as, unw_word_t ip, unw_proc_info_t* pi,
                 int need_unwind_info, void* arg) {
  SandboxedUnwindContext* ctx = static_cast<SandboxedUnwindContext*>(arg);
  auto it = std::find_if(
      ctx->maps.begin(), ctx->maps.end(),
      [ip](const auto& entry) { return entry.start <= ip && ip < entry.end; });
  if (it == ctx->maps.end()) {
    SAPI_RAW_LOG(ERROR, "No entry found for ip %lx",
                 reinterpret_cast<uintptr_t>(ip));
    return -UNW_ENOINFO;
  }

  absl::StatusOr<MMappedWrapper> mapped_image =
      MMappedWrapper::MapFile(it->path.c_str());
  if (!mapped_image.ok()) {
    SAPI_RAW_LOG(ERROR, "Failed to map elf image for path %s: %s",
                 it->path.c_str(),
                 std::string(mapped_image.status().message()).c_str());
    return -UNW_ENOINFO;
  }

  int ret = Sandbox2FindUnwindTable(
      as, mapped_image->data(), mapped_image->size(), it->path.c_str(),
      it->start, it->pgoff, ip, pi, need_unwind_info, ctx);
  return ret;
}

void PutUnwindInfo(unw_addr_space_t as, unw_proc_info_t* pi, void* arg) {
  free(pi->unwind_info);
  pi->unwind_info = nullptr;
}

int GetDynInfoListAddr(unw_addr_space_t as, unw_word_t* dil_addr, void* arg) {
  // libunwind-ptrace does not implement this except for IA64.
  // See: libunwind/src/ptrace/_UPT_get_dyn_info_list_addr.c
  return -UNW_ENOINFO;
}

int AccessMem(unw_addr_space_t as, unw_word_t addr, unw_word_t* val, int write,
              void* arg) {
  auto* ctx = static_cast<SandboxedUnwindContext*>(arg);
  if (write) {
    SAPI_RAW_LOG(INFO, "Unsupported operation: AccessMem write");
    return -UNW_ENOINFO;
  }
  if (pread(ctx->mem_fd.get(), val, sizeof(unw_word_t),
            reinterpret_cast<uintptr_t>(addr)) != sizeof(unw_word_t)) {
    SAPI_RAW_LOG(ERROR, "pread() failed for addr %lx",
                 reinterpret_cast<uintptr_t>(addr));
    return -UNW_ENOINFO;
  }
  return 0;
}

int AcessReg(unw_addr_space_t as, unw_regnum_t reg, unw_word_t* val, int write,
             void* arg) {
  auto* ctx = static_cast<SandboxedUnwindContext*>(arg);
  if (write) {
    SAPI_RAW_LOG(INFO, "Unsupported operation: AccessReg write");
    return -UNW_ENOINFO;
  }
  *val = GetReg(ctx->regs, reg);
  return 0;
}

int AccessFPReg(unw_addr_space_t as, unw_regnum_t reg, unw_fpreg_t* val,
                int write, void* arg) {
  SAPI_RAW_LOG(INFO, "Unsupported operation: AccessFPReg");
  return -UNW_ENOINFO;
}

int Resume(unw_addr_space_t as, unw_cursor_t* c, void* arg) {
  SAPI_RAW_LOG(INFO, "Unsupported operation: Resume");
  return -UNW_ENOINFO;
}

int GetProcName(unw_addr_space_t as, unw_word_t ip, char* buf, size_t buf_len,
                unw_word_t* offp, void* arg) {
  SAPI_RAW_LOG(INFO, "Unsupported operation: GetProcName");
  return -UNW_ENOINFO;
}

}  // namespace

unw_accessors_t* GetUnwindAccessors() {
  static unw_accessors_t accessors = {
      .find_proc_info = FindProcInfo,
      .put_unwind_info = PutUnwindInfo,
      .get_dyn_info_list_addr = GetDynInfoListAddr,
      .access_mem = AccessMem,
      .access_reg = AcessReg,
      .access_fpreg = AccessFPReg,
      .resume = Resume,
      .get_proc_name = GetProcName};
  return &accessors;
}

}  // namespace sandbox2
