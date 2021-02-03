# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(workdir "${CMAKE_BINARY_DIR}/_deps/libunwind-populate")

set(SAPI_LIBUNWIND_URL
  https://github.com/libunwind/libunwind/releases/download/v1.2.1/libunwind-1.2.1.tar.gz
  CACHE STRING "")
set(SAPI_LIBUNWIND_URL_HASH
  SHA256=3f3ecb90e28cbe53fba7a4a27ccce7aad188d3210bb1964a923a731a27a75acb
  CACHE STRING "")
set(SAPI_LIBUNWIND_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/libunwind-src"
                              CACHE STRING "")
set(SAPI_LIBUNWIND_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/libunwind-build"
                              CACHE STRING "")

file(WRITE "${workdir}/CMakeLists.txt" "\
cmake_minimum_required(VERSION ${CMAKE_VERSION})
project(libunwind-populate NONE)
include(ExternalProject)
ExternalProject_Add(libunwind
  URL               \"${SAPI_LIBUNWIND_URL}\"
  URL_HASH          \"${SAPI_LIBUNWIND_URL_HASH}\"
  SOURCE_DIR        \"${SAPI_LIBUNWIND_SOURCE_DIR}\"
  CONFIGURE_COMMAND ./configure
                    --disable-dependency-tracking
                    --disable-documentation
                    --disable-minidebuginfo
                    --disable-shared
                    --enable-ptrace
                    ${SAPI_THIRD_PARTY_CONFIGUREOPTS}
  BUILD_COMMAND     \"\"
  INSTALL_COMMAND   \"\"
  TEST_COMMAND      \"\"
  BUILD_IN_SOURCE TRUE
)
")

execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
                RESULT_VARIABLE error
                WORKING_DIRECTORY "${workdir}")
if(error)
  message(FATAL_ERROR "CMake step for ${PROJECT_NAME} failed: ${error}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build .
                RESULT_VARIABLE error
                WORKING_DIRECTORY "${workdir}")
if(error)
  message(FATAL_ERROR "Build step for ${PROJECT_NAME} failed: ${error}")
endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(_unwind_cpu "x86_64")
  list(APPEND _unwind_platform_srcs
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/Gcreate_addr_space.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/Gglobal.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/Ginit.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/Gos-linux.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/Gregs.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/Gresume.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/Gstash_frame.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/Gstep.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/is_fpreg.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/setcontext.S
  )
  list(APPEND _unwind_ptrace_srcs
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/Ginit_remote.c
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64")
  set(_unwind_cpu "ppc64")
  list(APPEND _unwind_platform_srcs
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ppc/Gis_signal_frame.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ppc64/Gcreate_addr_space.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ppc64/Gglobal.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ppc64/Ginit.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ppc64/Gregs.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ppc64/Gresume.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ppc64/Gstep.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ppc64/get_func_addr.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ppc64/is_fpreg.c
  )
  list(APPEND _unwind_ptrace_srcs
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ppc/Ginit_remote.c
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  set(_unwind_cpu "aarch64")
  list(APPEND _unwind_platform_srcs
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/aarch64/Gcreate_addr_space.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/aarch64/Gglobal.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/aarch64/Ginit.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/aarch64/Gis_signal_frame.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/aarch64/Gregs.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/aarch64/Gresume.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/aarch64/Gstash_frame.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/aarch64/Gstep.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/aarch64/is_fpreg.c
  )
  list(APPEND _unwind_ptrace_srcs
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/aarch64/Ginit_remote.c
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
  set(_unwind_cpu "arm")
  list(APPEND _unwind_platform_srcs
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/arm/Gcreate_addr_space.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/arm/Gex_tables.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/arm/Gglobal.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/arm/Ginit.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/arm/Gis_signal_frame.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/arm/Gregs.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/arm/Gresume.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/arm/Gstash_frame.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/arm/Gstep.c
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/arm/is_fpreg.c
  )
  list(APPEND _unwind_ptrace_srcs
    ${SAPI_LIBUNWIND_SOURCE_DIR}/src/arm/Ginit_remote.c
  )
endif()

add_library(unwind_ptrace_wrapped STATIC
  # internal_headers
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/compiler.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/config.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/dwarf.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/dwarf-eh.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/dwarf_i.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/libunwind.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/libunwind-common.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/libunwind-coredump.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/libunwind-dynamic.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/libunwind-ptrace.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/libunwind-x86_64.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/libunwind_i.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/mempool.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/remote.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/tdep-x86_64/dwarf-config.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/tdep-x86_64/libunwind_i.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/tdep/dwarf-config.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/tdep/libunwind_i.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/unwind.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/elf32.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/elf64.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/elfxx.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/os-linux.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/init.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/offsets.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/ucontext_i.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/x86_64/unwind_i.h
  # included_sources
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/elf64.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/elfxx.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/elfxx.c
  # sources_common
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/dwarf/Gexpr.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/dwarf/Gfde.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/dwarf/Gfind_proc_info-lsb.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/dwarf/Gfind_unwind_table.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/dwarf/Gparser.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/dwarf/Gpe.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/dwarf/Gstep.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/dwarf/global.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/mi/Gdestroy_addr_space.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/mi/Gdyn-extract.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/mi/Gfind_dynamic_proc_info.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/mi/Gget_accessors.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/mi/Gget_proc_name.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/mi/Gget_reg.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/mi/Gput_dynamic_unwind_info.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/mi/flush_cache.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/mi/init.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/mi/mempool.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/os-linux.c
  ${_unwind_platform_srcs}
  # srcs
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/mi/Gdyn-remote.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_access_fpreg.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_access_mem.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_access_reg.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_accessors.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_create.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_destroy.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_elf.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_find_proc_info.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_get_dyn_info_list_addr.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_get_proc_name.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_internal.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_put_unwind_info.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_reg_offset.c
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src/ptrace/_UPT_resume.c
  # hdrs
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/config.h
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/libunwind.h
  # source_ptrace
  ${_unwind_ptrace_srcs}
)
add_library(unwind::unwind_ptrace_wrapped ALIAS unwind_ptrace_wrapped)
target_include_directories(unwind_ptrace_wrapped PUBLIC
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/tdep
  ${SAPI_LIBUNWIND_SOURCE_DIR}/include/tdep-${_unwind_cpu}
  ${SAPI_LIBUNWIND_SOURCE_DIR}/src
)
target_compile_options(unwind_ptrace_wrapped PRIVATE
  -fno-common
  -Wno-cpp
)
target_compile_definitions(unwind_ptrace_wrapped
  PRIVATE -DHAVE_CONFIG_H
          -D_GNU_SOURCE
          -DNO_FRAME_POINTER
  PUBLIC -D_UPT_accessors=_UPT_accessors_wrapped
         -D_UPT_create=_UPT_create_wrapped
         -D_UPT_destroy=_UPT_destroy_wrapped

         -D_U${_unwind_cpu}_create_addr_space=_U${_unwind_cpu}_create_addr_space_wrapped
         -D_U${_unwind_cpu}_destroy_addr_space=_U${_unwind_cpu}_destroy_addr_space_wrapped
         -D_U${_unwind_cpu}_get_proc_name=_U${_unwind_cpu}_get_proc_name_wrapped
         -D_U${_unwind_cpu}_get_reg=_U${_unwind_cpu}_get_reg_wrapped
         -D_U${_unwind_cpu}_init_remote=_U${_unwind_cpu}_init_remote_wrapped
         -D_U${_unwind_cpu}_step=_U${_unwind_cpu}_step_wrapped

         -Dptrace=ptrace_wrapped
)
target_link_libraries(unwind_ptrace_wrapped PRIVATE
  sapi::base
  sandbox2::ptrace_hook
)
