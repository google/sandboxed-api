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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libunwind.h"
#include "libunwind_i.h"

int Sandbox2FindUnwindTable(unw_addr_space_t as, void* map, size_t map_size,
                            const char* path, unw_word_t segbase,
                            unw_word_t mapoff, unw_word_t ip,
                            unw_proc_info_t* pi, int need_unwind_info,
                            void* arg) {
  struct elf_dyn_info edi;
  memset(&edi, 0, sizeof(edi));
  invalidate_edi(&edi);
  edi.ei.image = map;
  edi.ei.size = map_size;

  if (tdep_find_unwind_table(&edi, as, path, segbase, mapoff, ip) < 0) {
    return -UNW_ENOINFO;
  }

  int ret = -UNW_ENOINFO;
  if (ret == -UNW_ENOINFO && edi.di_cache.format != -1) {
    ret = tdep_search_unwind_table(as, ip, &edi.di_cache, pi, need_unwind_info,
                                   arg);
  }

  if (ret == -UNW_ENOINFO && edi.di_debug.format != -1) {
    ret = tdep_search_unwind_table(as, ip, &edi.di_debug, pi, need_unwind_info,
                                   arg);
  }

#if UNW_TARGET_ARM
  if (ret == -UNW_ENOINFO && edi.di_arm.format != -1)
    ret = tdep_search_unwind_table(as, ip, &edi.di_arm, pi, need_unwind_info,
                                   arg);
#endif

  return ret;
}
