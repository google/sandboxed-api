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

#include "sandboxed_api/sandbox2/unwind/unwind.h"

#include <cxxabi.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "libunwind-ptrace.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/unwind/ptrace_hook.h"
#include "sandboxed_api/sandbox2/unwind/unwind.pb.h"
#include "sandboxed_api/sandbox2/util/maps_parser.h"
#include "sandboxed_api/sandbox2/util/minielf.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {
namespace {

std::string DemangleSymbol(const std::string& maybe_mangled) {
  int status;
  std::unique_ptr<char, std::function<void(char*)>> symbol = {
      abi::__cxa_demangle(maybe_mangled.c_str(), nullptr, nullptr, &status),
      free};
  if (symbol && status == 0) {
    return symbol.get();
  }
  return maybe_mangled;
}

std::string GetSymbolAt(const std::map<uint64_t, std::string>& addr_to_symbol,
                        uint64_t addr) {
  auto entry_for_next_symbol = addr_to_symbol.lower_bound(addr);
  if (entry_for_next_symbol != addr_to_symbol.end() &&
      entry_for_next_symbol != addr_to_symbol.begin()) {
    // Matches the addr exactly:
    if (entry_for_next_symbol->first == addr) {
      return DemangleSymbol(entry_for_next_symbol->second);
    }

    // Might be inside a function, return symbol+offset;
    const auto entry_for_previous_symbol = --entry_for_next_symbol;
    if (!entry_for_previous_symbol->second.empty()) {
      return absl::StrCat(DemangleSymbol(entry_for_previous_symbol->second),
                          "+0x",
                          absl::Hex(addr - entry_for_previous_symbol->first));
    }
  }
  return "";
}

}  // namespace

std::vector<uintptr_t> GetIPList(pid_t pid, int max_frames) {
  unw_cursor_t cursor;
  static unw_addr_space_t as =
      unw_create_addr_space(&_UPT_accessors, 0 /* byte order */);
  if (as == nullptr) {
    SAPI_RAW_LOG(WARNING, "unw_create_addr_space() failed");
    return {};
  }

  std::unique_ptr<struct UPT_info, void (*)(void*)> ui(
      reinterpret_cast<struct UPT_info*>(_UPT_create(pid)), _UPT_destroy);
  if (ui == nullptr) {
    SAPI_RAW_LOG(WARNING, "_UPT_create() failed");
    return {};
  }

  int rc = unw_init_remote(&cursor, as, ui.get());
  if (rc < 0) {
    // Could be UNW_EINVAL (8), UNW_EUNSPEC (1) or UNW_EBADREG (3).
    SAPI_RAW_LOG(WARNING, "unw_init_remote() failed with error %d", rc);
    return {};
  }

  std::vector<uintptr_t> ips;
  for (int i = 0; i < max_frames; i++) {
    unw_word_t ip;
    rc = unw_get_reg(&cursor, UNW_REG_IP, &ip);
    if (rc < 0) {
      // Could be UNW_EUNSPEC or UNW_EBADREG.
      SAPI_RAW_LOG(WARNING, "unw_get_reg() failed with error %d", rc);
      break;
    }
    ips.push_back(ip);
    rc = unw_step(&cursor);
    // Non-error condition: UNW_ESUCCESS (0).
    if (rc < 0) {
      // If anything but UNW_ESTOPUNWIND (-5), there has been an error.
      // However since we can't do anything about it and it appears that
      // this happens every time we don't log this.
      break;
    }
  }
  return ips;
}

bool RunLibUnwindAndSymbolizer(Comms* comms) {
  UnwindSetup setup;
  if (!comms->RecvProtoBuf(&setup)) {
    return false;
  }

  const std::string& data = setup.regs();
  InstallUserRegs(data.c_str(), data.length());
  ArmPtraceEmulation();

  std::vector<uintptr_t> ips;
  std::vector<std::string> stack_trace =
      RunLibUnwindAndSymbolizer(setup.pid(), &ips, setup.default_max_frames());

  UnwindResult msg;
  *msg.mutable_stacktrace() = {stack_trace.begin(), stack_trace.end()};
  *msg.mutable_ip() = {ips.begin(), ips.end()};
  return comms->SendProtoBuf(msg);
}

std::vector<std::string> RunLibUnwindAndSymbolizer(pid_t pid,
                                                   std::vector<uintptr_t>* ips,
                                                   int max_frames) {
  // Run libunwind.
  *ips = GetIPList(pid, max_frames);

  // Open /proc/pid/maps.
  std::string path_maps = absl::StrCat("/proc/", pid, "/maps");
  std::unique_ptr<FILE, void (*)(FILE*)> f(fopen(path_maps.c_str(), "r"),
                                           [](FILE* s) {
                                             if (s) {
                                               fclose(s);
                                             }
                                           });
  if (!f) {
    // Could not open maps file.
    SAPI_RAW_LOG(ERROR, "Could not open %s", path_maps);
    return {};
  }

  constexpr static size_t kBufferSize = 10 * 1024 * 1024;
  std::string maps_content(kBufferSize, '\0');
  size_t bytes_read = fread(&maps_content[0], 1, kBufferSize, f.get());
  if (bytes_read == 0) {
    // Could not read the whole maps file.
    SAPI_RAW_PLOG(ERROR, "Could not read maps file");
    return {};
  }
  maps_content.resize(bytes_read);

  auto maps = ParseProcMaps(maps_content);
  if (!maps.ok()) {
    SAPI_RAW_LOG(ERROR, "Could not parse /proc/%d/maps", pid);
    return {};
  }

  // Get symbols for each file entry in the maps entry.
  // This is not a very efficient way, so we might want to optimize it.
  std::map<uint64_t, std::string> addr_to_symbol;
  for (const auto& entry : *maps) {
    if (!entry.path.empty()) {
      // Store details about start + end of this map.
      // The maps entries are ordered and thus sorted with increasing adresses.
      // This means if there is a symbol @ entry.end, it will be overwritten in
      // the next iteration.
      addr_to_symbol[entry.start] = absl::StrCat("map:", entry.path);
      addr_to_symbol[entry.end] = "";
    }

    const bool should_parse = entry.is_executable && !entry.path.empty() &&
                              entry.path != "[vdso]" &&
                              entry.path != "[vsyscall]";
    if (should_parse) {
      auto elf_or = ElfFile::ParseFromFile(entry.path, ElfFile::kLoadSymbols);
      if (!elf_or.ok()) {
        SAPI_RAW_LOG(WARNING, "Could not load symbols for %s: %s", entry.path,
                     elf_or.status().message());
        continue;
      }
      auto elf = std::move(elf_or).value();

      for (const auto& symbol : elf.symbols()) {
        if (elf.position_independent()) {
          if (symbol.address < entry.end - entry.start) {
            addr_to_symbol[symbol.address + entry.start] = symbol.name;
          }
        } else {
          if (symbol.address >= entry.start && symbol.address < entry.end) {
            addr_to_symbol[symbol.address] = symbol.name;
          }
        }
      }
    }
  }

  std::vector<std::string> stack_trace;
  stack_trace.reserve(ips->size());
  // Symbolize stacktrace.
  for (const auto& ip : *ips) {
    const std::string symbol =
        GetSymbolAt(addr_to_symbol, static_cast<uint64_t>(ip));
    stack_trace.push_back(absl::StrCat(symbol, "(0x", absl::Hex(ip), ")"));
  }
  return stack_trace;
}

}  // namespace sandbox2
