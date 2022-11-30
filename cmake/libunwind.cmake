# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

FetchContent_Declare(libunwind
  URL https://github.com/libunwind/libunwind/releases/download/v1.6.2/libunwind-1.6.2.tar.gz
  URL_HASH SHA256=4a6aec666991fb45d0889c44aede8ad6eb108071c3554fcdff671f9c94794976
)
FetchContent_GetProperties(libunwind)
if(NOT libunwind_POPULATED)
  FetchContent_Populate(libunwind)
  set(libunwind_STATUS_FILE "${libunwind_SOURCE_DIR}/config.status")
  if(EXISTS "${libunwind_STATUS_FILE}")
    file(SHA256 "${libunwind_STATUS_FILE}" _sapi_CONFIG_STATUS)
  endif()
  if(NOT _sapi_CONFIG_STATUS STREQUAL "${libunwind_CONFIG_STATUS}")
    message("-- Running ./configure for libunwind...")
    execute_process(
      COMMAND ./configure --disable-dependency-tracking
                          --disable-minidebuginfo
                          --disable-shared
                          --enable-ptrace
                          --quiet
      WORKING_DIRECTORY "${libunwind_SOURCE_DIR}"
      RESULT_VARIABLE _sapi_libunwind_config_result
    )
    if(NOT _sapi_libunwind_config_result EQUAL "0")
      message(FATAL_ERROR "Configuration of libunwind dependency failed")
    endif()
    file(SHA256 "${libunwind_SOURCE_DIR}/config.status" _sapi_CONFIG_STATUS)
    set(libunwind_CONFIG_STATUS "${_sapi_CONFIG_STATUS}" CACHE INTERNAL "")
  endif()
endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(_unwind_cpu "x86_64")
  list(APPEND _unwind_platform_srcs
    ${libunwind_SOURCE_DIR}/src/x86_64/Gcreate_addr_space.c
    ${libunwind_SOURCE_DIR}/src/x86_64/Gglobal.c
    ${libunwind_SOURCE_DIR}/src/x86_64/Ginit.c
    ${libunwind_SOURCE_DIR}/src/x86_64/Gos-linux.c
    ${libunwind_SOURCE_DIR}/src/x86_64/Gregs.c
    ${libunwind_SOURCE_DIR}/src/x86_64/Gresume.c
    ${libunwind_SOURCE_DIR}/src/x86_64/Gstash_frame.c
    ${libunwind_SOURCE_DIR}/src/x86_64/Gstep.c
    ${libunwind_SOURCE_DIR}/src/x86_64/is_fpreg.c
    ${libunwind_SOURCE_DIR}/src/x86_64/setcontext.S
  )
  list(APPEND _unwind_ptrace_srcs
    ${libunwind_SOURCE_DIR}/src/x86_64/Ginit_remote.c
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64")
  set(_unwind_cpu "ppc64")
  list(APPEND _unwind_platform_srcs
    ${libunwind_SOURCE_DIR}/src/ppc/Gis_signal_frame.c
    ${libunwind_SOURCE_DIR}/src/ppc64/Gcreate_addr_space.c
    ${libunwind_SOURCE_DIR}/src/ppc64/Gglobal.c
    ${libunwind_SOURCE_DIR}/src/ppc64/Ginit.c
    ${libunwind_SOURCE_DIR}/src/ppc64/Gregs.c
    ${libunwind_SOURCE_DIR}/src/ppc64/Gresume.c
    ${libunwind_SOURCE_DIR}/src/ppc64/Gstep.c
    ${libunwind_SOURCE_DIR}/src/ppc64/get_func_addr.c
    ${libunwind_SOURCE_DIR}/src/ppc64/is_fpreg.c
  )
  list(APPEND _unwind_ptrace_srcs
    ${libunwind_SOURCE_DIR}/src/ppc/Ginit_remote.c
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  set(_unwind_cpu "aarch64")
  list(APPEND _unwind_platform_srcs
    ${libunwind_SOURCE_DIR}/src/aarch64/Gcreate_addr_space.c
    ${libunwind_SOURCE_DIR}/src/aarch64/Gglobal.c
    ${libunwind_SOURCE_DIR}/src/aarch64/Ginit.c
    ${libunwind_SOURCE_DIR}/src/aarch64/Gis_signal_frame.c
    ${libunwind_SOURCE_DIR}/src/aarch64/Gregs.c
    ${libunwind_SOURCE_DIR}/src/aarch64/Gresume.c
    ${libunwind_SOURCE_DIR}/src/aarch64/Gstash_frame.c
    ${libunwind_SOURCE_DIR}/src/aarch64/Gstep.c
    ${libunwind_SOURCE_DIR}/src/aarch64/is_fpreg.c
  )
  list(APPEND _unwind_ptrace_srcs
    ${libunwind_SOURCE_DIR}/src/aarch64/Ginit_remote.c
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
  set(_unwind_cpu "arm")
  list(APPEND _unwind_platform_srcs
    ${libunwind_SOURCE_DIR}/src/arm/Gcreate_addr_space.c
    ${libunwind_SOURCE_DIR}/src/arm/Gex_tables.c
    ${libunwind_SOURCE_DIR}/src/arm/Gglobal.c
    ${libunwind_SOURCE_DIR}/src/arm/Ginit.c
    ${libunwind_SOURCE_DIR}/src/arm/Gis_signal_frame.c
    ${libunwind_SOURCE_DIR}/src/arm/Gregs.c
    ${libunwind_SOURCE_DIR}/src/arm/Gresume.c
    ${libunwind_SOURCE_DIR}/src/arm/Gstash_frame.c
    ${libunwind_SOURCE_DIR}/src/arm/Gstep.c
    ${libunwind_SOURCE_DIR}/src/arm/is_fpreg.c
  )
  list(APPEND _unwind_ptrace_srcs
    ${libunwind_SOURCE_DIR}/src/arm/Ginit_remote.c
  )
endif()

add_library(unwind_ptrace STATIC
  # internal_headers
  ${libunwind_SOURCE_DIR}/include/compiler.h
  ${libunwind_SOURCE_DIR}/include/config.h
  ${libunwind_SOURCE_DIR}/include/dwarf.h
  ${libunwind_SOURCE_DIR}/include/dwarf-eh.h
  ${libunwind_SOURCE_DIR}/include/dwarf_i.h
  ${libunwind_SOURCE_DIR}/include/libunwind.h
  ${libunwind_SOURCE_DIR}/include/libunwind-common.h
  ${libunwind_SOURCE_DIR}/include/libunwind-coredump.h
  ${libunwind_SOURCE_DIR}/include/libunwind-dynamic.h
  ${libunwind_SOURCE_DIR}/include/libunwind-ptrace.h
  ${libunwind_SOURCE_DIR}/include/libunwind-x86_64.h
  ${libunwind_SOURCE_DIR}/include/libunwind_i.h
  ${libunwind_SOURCE_DIR}/include/mempool.h
  ${libunwind_SOURCE_DIR}/include/remote.h
  ${libunwind_SOURCE_DIR}/include/tdep-x86_64/dwarf-config.h
  ${libunwind_SOURCE_DIR}/include/tdep-x86_64/libunwind_i.h
  ${libunwind_SOURCE_DIR}/include/tdep/dwarf-config.h
  ${libunwind_SOURCE_DIR}/include/tdep/libunwind_i.h
  ${libunwind_SOURCE_DIR}/include/unwind.h
  ${libunwind_SOURCE_DIR}/src/elf32.h
  ${libunwind_SOURCE_DIR}/src/elf64.h
  ${libunwind_SOURCE_DIR}/src/elfxx.h
  ${libunwind_SOURCE_DIR}/src/os-linux.h
  ${libunwind_SOURCE_DIR}/src/x86_64/init.h
  ${libunwind_SOURCE_DIR}/src/x86_64/offsets.h
  ${libunwind_SOURCE_DIR}/src/x86_64/ucontext_i.h
  ${libunwind_SOURCE_DIR}/src/x86_64/unwind_i.h
  # included_sources
  ${libunwind_SOURCE_DIR}/src/elf64.h
  ${libunwind_SOURCE_DIR}/src/elfxx.h
  ${libunwind_SOURCE_DIR}/src/elfxx.c
  # sources_common
  ${libunwind_SOURCE_DIR}/src/dwarf/Gexpr.c
  ${libunwind_SOURCE_DIR}/src/dwarf/Gfde.c
  ${libunwind_SOURCE_DIR}/src/dwarf/Gfind_proc_info-lsb.c
  ${libunwind_SOURCE_DIR}/src/dwarf/Gfind_unwind_table.c
  ${libunwind_SOURCE_DIR}/src/dwarf/Gparser.c
  ${libunwind_SOURCE_DIR}/src/dwarf/Gpe.c
  ${libunwind_SOURCE_DIR}/src/dwarf/global.c
  ${libunwind_SOURCE_DIR}/src/mi/Gdestroy_addr_space.c
  ${libunwind_SOURCE_DIR}/src/mi/Gdyn-extract.c
  ${libunwind_SOURCE_DIR}/src/mi/Gfind_dynamic_proc_info.c
  ${libunwind_SOURCE_DIR}/src/mi/Gget_accessors.c
  ${libunwind_SOURCE_DIR}/src/mi/Gget_proc_name.c
  ${libunwind_SOURCE_DIR}/src/mi/Gget_reg.c
  ${libunwind_SOURCE_DIR}/src/mi/Gput_dynamic_unwind_info.c
  ${libunwind_SOURCE_DIR}/src/mi/flush_cache.c
  ${libunwind_SOURCE_DIR}/src/mi/init.c
  ${libunwind_SOURCE_DIR}/src/mi/mempool.c
  ${libunwind_SOURCE_DIR}/src/os-linux.c
  ${_unwind_platform_srcs}
  # srcs
  ${libunwind_SOURCE_DIR}/src/mi/Gdyn-remote.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_access_fpreg.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_access_mem.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_access_reg.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_accessors.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_create.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_destroy.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_elf.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_find_proc_info.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_get_dyn_info_list_addr.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_get_proc_name.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_internal.h
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_put_unwind_info.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_reg_offset.c
  ${libunwind_SOURCE_DIR}/src/ptrace/_UPT_resume.c
  # hdrs
  ${libunwind_SOURCE_DIR}/include/config.h
  ${libunwind_SOURCE_DIR}/include/libunwind.h
  # source_ptrace
  ${_unwind_ptrace_srcs}
)
add_library(unwind::unwind_ptrace ALIAS unwind_ptrace)
target_include_directories(unwind_ptrace PUBLIC
  ${libunwind_SOURCE_DIR}/include
  ${libunwind_SOURCE_DIR}/include/tdep
  ${libunwind_SOURCE_DIR}/include/tdep-${_unwind_cpu}
  ${libunwind_SOURCE_DIR}/src
)
target_compile_options(unwind_ptrace PRIVATE
  -fno-common
  -Wno-cpp
)
target_compile_definitions(unwind_ptrace
  PRIVATE -DHAVE_CONFIG_H
          -D_GNU_SOURCE
          -DNO_FRAME_POINTER
)
target_link_libraries(unwind_ptrace PRIVATE
  sapi::base
)
