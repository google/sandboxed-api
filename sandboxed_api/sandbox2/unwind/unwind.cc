// Copyright 2019 Google LLC. All Rights Reserved.
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

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "external/org_gnu_libunwind/include/libunwind-ptrace.h"
#include "sandboxed_api/sandbox2/comms.h"
#include "sandboxed_api/sandbox2/unwind/unwind.pb.h"
#include "sandboxed_api/sandbox2/util/maps_parser.h"
#include "sandboxed_api/sandbox2/util/minielf.h"
#include "sandboxed_api/sandbox2/util/strerror.h"
#include "sandboxed_api/util/raw_logging.h"

namespace sandbox2 {
namespace {

std::string GetSymbolAt(const std::map<uint64_t, std::string>& addr_to_symbol,
                   uint64_t addr) {
  auto entry_for_next_symbol = addr_to_symbol.lower_bound(addr);
  if (entry_for_next_symbol != addr_to_symbol.end() &&
      entry_for_next_symbol != addr_to_symbol.begin()) {
    // Matches the addr exactly:
    if (entry_for_next_symbol->first == addr) {
      return entry_for_next_symbol->second;
    }

    // Might be inside a function, return symbol+offset;
    const auto entry_for_previous_symbol = --entry_for_next_symbol;
    if (!entry_for_previous_symbol->second.empty()) {
      return absl::StrCat(entry_for_previous_symbol->second, "+0x",
                          absl::Hex(addr - entry_for_previous_symbol->first));
    }
  }
  return "";
}

}  // namespace

void GetIPList(pid_t pid, std::vector<uintptr_t>* ips, int max_frames) {
  ips->clear();

  unw_cursor_t cursor;
  static unw_addr_space_t as =
      unw_create_addr_space(&_UPT_accessors, 0 /* byte order */);
  if (as == nullptr) {
    SAPI_RAW_LOG(WARNING, "unw_create_addr_space() failed");
    return;
  }

  auto* ui = reinterpret_cast<struct UPT_info*>(_UPT_create(pid));
  if (ui == nullptr) {
    SAPI_RAW_LOG(WARNING, "_UPT_create() failed");
    return;
  }

  int rc = unw_init_remote(&cursor, as, ui);
  if (rc < 0) {
    // Could be UNW_EINVAL (8), UNW_EUNSPEC (1) or UNW_EBADREG (3).
    SAPI_RAW_LOG(WARNING, "unw_init_remote() failed with error %d", rc);
  } else {
    for (int i = 0; i < max_frames; i++) {
      unw_word_t ip;
      rc = unw_get_reg(&cursor, UNW_REG_IP, &ip);
      if (rc < 0) {
        // Could be UNW_EUNSPEC or UNW_EBADREG.
        SAPI_RAW_LOG(WARNING, "unw_get_reg() failed with error %d", rc);
        break;
      }
      ips->push_back(ip);
      rc = unw_step(&cursor);
      // Non-error condition: UNW_ESUCCESS (0).
      if (rc < 0) {
        // If anything but UNW_ESTOPUNWIND (-5), there has been an error.
        // However since we can't do anything about it and it appears that
        // this happens every time we don't log this.
        break;
      }
    }
  }

  // This is only needed if _UPT_create() has been successful.
  _UPT_destroy(ui);
}

void RunLibUnwindAndSymbolizer(pid_t pid, Comms* comms, int max_frames,
                               const std::string& delim) {
  UnwindResult msg;
  std::string stack_trace;
  std::vector<uintptr_t> ips;

  RunLibUnwindAndSymbolizer(pid, &stack_trace, &ips, max_frames, delim);
  for (const auto& i : ips) {
    msg.add_ip(i);
  }
  msg.set_stacktrace(stack_trace.c_str(), stack_trace.size());
  comms->SendProtoBuf(msg);
}

void RunLibUnwindAndSymbolizer(pid_t pid, std::string* stack_trace_out,
                               std::vector<uintptr_t>* ips, int max_frames,
                               const std::string& delim) {
  // Run libunwind.
  GetIPList(pid, ips, max_frames);

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
    return;
  }

  constexpr static size_t kBufferSize = 10 * 1024 * 1024;
  std::string maps_content(kBufferSize, '\0');
  size_t bytes_read = fread(&maps_content[0], 1, kBufferSize, f.get());
  if (bytes_read == 0) {
    // Could not read the whole maps file.
    SAPI_RAW_PLOG(ERROR, "Could not read maps file");
    return;
  }
  maps_content.resize(bytes_read);

  auto maps_or = ParseProcMaps(maps_content);
  if (!maps_or.ok()) {
    SAPI_RAW_LOG(ERROR, "Could not parse /proc/%d/maps", pid);
    return;
  }
  auto maps = std::move(maps_or).ValueOrDie();

  // Get symbols for each file entry in the maps entry.
  // This is not a very efficient way, so we might want to optimize it.
  std::map<uint64_t, std::string> addr_to_symbol;
  for (const auto& entry : maps) {
    if (!entry.path.empty()) {
      // Store details about start + end of this map.
      // The maps entries are ordered and thus sorted with increasing adresses.
      // This means if there is a symbol @ entry.end, it will be overwritten in
      // the next iteration.
      addr_to_symbol[entry.start] = absl::StrCat("map:", entry.path);
      addr_to_symbol[entry.end] = "";
    }

    if (!entry.path.empty() && entry.is_executable) {
      auto elf_or = ElfFile::ParseFromFile(entry.path, ElfFile::kLoadSymbols);
      if (!elf_or.ok()) {
        SAPI_RAW_LOG(WARNING, "Could not load symbols for %s: %s", entry.path,
                     elf_or.status().message());
        continue;
      }
      auto elf = std::move(elf_or).ValueOrDie();

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

  std::string stack_trace;
  // Symbolize stacktrace.
  for (auto i = ips->begin(); i != ips->end(); ++i) {
    if (i != ips->begin()) {
      stack_trace += delim;
    }
    std::string symbol = GetSymbolAt(addr_to_symbol, static_cast<uint64_t>(*i));
    absl::StrAppend(&stack_trace, symbol, "(0x", absl::Hex(*i), ")");
  }

  *stack_trace_out = stack_trace;
}

}  // namespace sandbox2
