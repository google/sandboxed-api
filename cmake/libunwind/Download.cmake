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

# Downloads and unpacks libunwind at configure time

set(workdir "${CMAKE_BINARY_DIR}/libunwind-download")

configure_file("${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in"
               "${workdir}/CMakeLists.txt")
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

set(_unwind_src "${CMAKE_BINARY_DIR}/libunwind-src")

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(_unwind_cpu "x86_64")
  list(APPEND _unwind_platform_srcs
    ${_unwind_src}/src/x86_64/Gcreate_addr_space.c
    ${_unwind_src}/src/x86_64/Gglobal.c
    ${_unwind_src}/src/x86_64/Ginit.c
    ${_unwind_src}/src/x86_64/Gos-linux.c
    ${_unwind_src}/src/x86_64/Gregs.c
    ${_unwind_src}/src/x86_64/Gresume.c
    ${_unwind_src}/src/x86_64/Gstash_frame.c
    ${_unwind_src}/src/x86_64/Gstep.c
    ${_unwind_src}/src/x86_64/is_fpreg.c
    ${_unwind_src}/src/x86_64/setcontext.S
  )
  list(APPEND _unwind_ptrace_srcs
    ${_unwind_src}/src/x86_64/Ginit_remote.c
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64")
  set(_unwind_cpu "ppc64")
  list(APPEND _unwind_platform_srcs
    ${_unwind_src}/src/ppc/Gis_signal_frame.c
    ${_unwind_src}/src/ppc64/Gcreate_addr_space.c
    ${_unwind_src}/src/ppc64/Gglobal.c
    ${_unwind_src}/src/ppc64/Ginit.c
    ${_unwind_src}/src/ppc64/Gregs.c
    ${_unwind_src}/src/ppc64/Gresume.c
    ${_unwind_src}/src/ppc64/Gstep.c
    ${_unwind_src}/src/ppc64/get_func_addr.c
    ${_unwind_src}/src/ppc64/is_fpreg.c
  )
  list(APPEND _unwind_ptrace_srcs
    ${_unwind_src}/src/ppc/Ginit_remote.c
  )
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  set(_unwind_cpu "aarch64")
  list(APPEND _unwind_platform_srcs
    ${_unwind_src}/src/aarch64/Gcreate_addr_space.c
    ${_unwind_src}/src/aarch64/Gglobal.c
    ${_unwind_src}/src/aarch64/Ginit.c
    ${_unwind_src}/src/aarch64/Gis_signal_frame.c
    ${_unwind_src}/src/aarch64/Gregs.c
    ${_unwind_src}/src/aarch64/Gresume.c
    ${_unwind_src}/src/aarch64/Gstash_frame.c
    ${_unwind_src}/src/aarch64/Gstep.c
    ${_unwind_src}/src/aarch64/is_fpreg.c
  )
  list(APPEND _unwind_ptrace_srcs
    ${_unwind_src}/src/aarch64/Ginit_remote.c
  )
endif()

add_library(unwind_ptrace_wrapped STATIC
  # internal_headers
  ${_unwind_src}/include/compiler.h
  ${_unwind_src}/include/config.h
  ${_unwind_src}/include/dwarf.h
  ${_unwind_src}/include/dwarf-eh.h
  ${_unwind_src}/include/dwarf_i.h
  ${_unwind_src}/include/libunwind.h
  ${_unwind_src}/include/libunwind-common.h
  ${_unwind_src}/include/libunwind-coredump.h
  ${_unwind_src}/include/libunwind-dynamic.h
  ${_unwind_src}/include/libunwind-ptrace.h
  ${_unwind_src}/include/libunwind-x86_64.h
  ${_unwind_src}/include/libunwind_i.h
  ${_unwind_src}/include/mempool.h
  ${_unwind_src}/include/remote.h
  ${_unwind_src}/include/tdep-x86_64/dwarf-config.h
  ${_unwind_src}/include/tdep-x86_64/libunwind_i.h
  ${_unwind_src}/include/tdep/dwarf-config.h
  ${_unwind_src}/include/tdep/libunwind_i.h
  ${_unwind_src}/include/unwind.h
  ${_unwind_src}/src/elf32.h
  ${_unwind_src}/src/elf64.h
  ${_unwind_src}/src/elfxx.h
  ${_unwind_src}/src/os-linux.h
  ${_unwind_src}/src/x86_64/init.h
  ${_unwind_src}/src/x86_64/offsets.h
  ${_unwind_src}/src/x86_64/ucontext_i.h
  ${_unwind_src}/src/x86_64/unwind_i.h
  # included_sources
  ${_unwind_src}/src/elf64.h
  ${_unwind_src}/src/elfxx.h
  ${_unwind_src}/src/elfxx.c
  # sources_common
  ${_unwind_src}/src/dwarf/Gexpr.c
  ${_unwind_src}/src/dwarf/Gfde.c
  ${_unwind_src}/src/dwarf/Gfind_proc_info-lsb.c
  ${_unwind_src}/src/dwarf/Gfind_unwind_table.c
  ${_unwind_src}/src/dwarf/Gparser.c
  ${_unwind_src}/src/dwarf/Gpe.c
  ${_unwind_src}/src/dwarf/Gstep.c
  ${_unwind_src}/src/dwarf/global.c
  ${_unwind_src}/src/mi/Gdestroy_addr_space.c
  ${_unwind_src}/src/mi/Gdyn-extract.c
  ${_unwind_src}/src/mi/Gfind_dynamic_proc_info.c
  ${_unwind_src}/src/mi/Gget_accessors.c
  ${_unwind_src}/src/mi/Gget_proc_name.c
  ${_unwind_src}/src/mi/Gget_reg.c
  ${_unwind_src}/src/mi/Gput_dynamic_unwind_info.c
  ${_unwind_src}/src/mi/flush_cache.c
  ${_unwind_src}/src/mi/init.c
  ${_unwind_src}/src/mi/mempool.c
  ${_unwind_src}/src/os-linux.c
  ${_unwind_platform_srcs}
  # srcs
  ${_unwind_src}/src/mi/Gdyn-remote.c
  ${_unwind_src}/src/ptrace/_UPT_access_fpreg.c
  ${_unwind_src}/src/ptrace/_UPT_access_mem.c
  ${_unwind_src}/src/ptrace/_UPT_access_reg.c
  ${_unwind_src}/src/ptrace/_UPT_accessors.c
  ${_unwind_src}/src/ptrace/_UPT_create.c
  ${_unwind_src}/src/ptrace/_UPT_destroy.c
  ${_unwind_src}/src/ptrace/_UPT_elf.c
  ${_unwind_src}/src/ptrace/_UPT_find_proc_info.c
  ${_unwind_src}/src/ptrace/_UPT_get_dyn_info_list_addr.c
  ${_unwind_src}/src/ptrace/_UPT_get_proc_name.c
  ${_unwind_src}/src/ptrace/_UPT_internal.h
  ${_unwind_src}/src/ptrace/_UPT_put_unwind_info.c
  ${_unwind_src}/src/ptrace/_UPT_reg_offset.c
  ${_unwind_src}/src/ptrace/_UPT_resume.c
  # hdrs
  ${_unwind_src}/include/config.h
  ${_unwind_src}/include/libunwind.h
  # source_ptrace
  ${_unwind_ptrace_srcs}
)
add_library(unwind::unwind_ptrace_wrapped ALIAS unwind_ptrace_wrapped)
target_include_directories(unwind_ptrace_wrapped PUBLIC
  ${_unwind_src}/include
  ${_unwind_src}/include/tdep
  ${_unwind_src}/src
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
