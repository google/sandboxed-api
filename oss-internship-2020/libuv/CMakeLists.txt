# Copyright 2020 Google LLC
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

cmake_minimum_required(VERSION 3.12)

project(libuv_sandbox)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add SAPI directories
add_subdirectory(
  "${SAPI_ROOT}"
  "${CMAKE_BINARY_DIR}/sandboxed-api-build"
  EXCLUDE_FROM_ALL
)

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/uv_wrapper")
add_custom_command(

  OUTPUT "${CMAKE_BINARY_DIR}/uv_wrapper/uv_wrapper.h"
         "${CMAKE_BINARY_DIR}/uv_wrapper/uv_wrapper.cc"

  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"

  COMMAND "${SAPI_PYTHON3_EXECUTABLE}"
          "generator/wrapper_generator.py"
          "libuv/include/uv.h"
          "${CMAKE_BINARY_DIR}/uv_wrapper/uv_wrapper.h"
          "${CMAKE_BINARY_DIR}/uv_wrapper/uv_wrapper.cc"

  COMMENT "Generate the wrapper header and source files"
)

option(SAPI_UV_ENABLE_EXAMPLES "" ON)
option(SAPI_UV_ENABLE_TESTS "" ON)

# Add callbacks used by examples and tests
if (SAPI_UV_ENABLE_EXAMPLES OR SAPI_UV_ENABLE_TESTS)
  list(APPEND SAPI_UV_CALLBACKS
    "${CMAKE_CURRENT_SOURCE_DIR}/callbacks/callbacks.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/callbacks/callbacks.cc"
  )
endif()

# Wrapper library including wrappers for all libuv methods and callbacks
# The SAPI_UV_CALLBACKS variable should contain the absolute paths of
# all the files implementing the callbacks
add_library(uv_wrapper_and_callbacks OBJECT
  "${CMAKE_BINARY_DIR}/uv_wrapper/uv_wrapper.h"
  "${CMAKE_BINARY_DIR}/uv_wrapper/uv_wrapper.cc"
  "${SAPI_UV_CALLBACKS}"
)
set_target_properties(uv_wrapper_and_callbacks
  PROPERTIES LINKER_LANGUAGE C
)

# Link the wrapper to the original uv library
add_subdirectory(libuv)
target_link_libraries(uv_wrapper_and_callbacks uv_a)

# Setup Sandboxed API
set(SAPI_ROOT "" CACHE PATH "Path to the Sandboxed API source tree")
set(SAPI_BUILD_EXAMPLES ${SAPI_UV_ENABLE_EXAMPLES} CACHE BOOL "" FORCE)
set(SAPI_BUILD_TESTING ${SAPI_UV_ENABLE_TESTS} CACHE BOOL "" FORCE)

# Generate SAPI header
add_sapi_library(uv_sapi

  # List of all the generated methods
  FUNCTIONS sapi_uv_accept
            sapi_uv_async_init
            sapi_uv_async_send
            sapi_uv_backend_fd
            sapi_uv_backend_timeout
            sapi_uv_barrier_destroy
            sapi_uv_barrier_init
            sapi_uv_barrier_wait
            # TODO(cblichmann): sapi_uv_buf_init
            sapi_uv_cancel
            sapi_uv_chdir
            sapi_uv_check_init
            sapi_uv_check_start
            sapi_uv_check_stop
            sapi_uv_close
            sapi_uv_cond_broadcast
            sapi_uv_cond_destroy
            sapi_uv_cond_init
            sapi_uv_cond_signal
            sapi_uv_cond_timedwait
            sapi_uv_cond_wait
            sapi_uv_cpu_info
            sapi_uv_cwd
            sapi_uv_default_loop
            sapi_uv_disable_stdio_inheritance
            sapi_uv_dlclose
            sapi_uv_dlerror
            sapi_uv_dlopen
            sapi_uv_dlsym
            sapi_uv_err_name
            sapi_uv_err_name_r
            sapi_uv_exepath
            sapi_uv_fileno
            sapi_uv_free_cpu_info
            sapi_uv_free_interface_addresses
            sapi_uv_freeaddrinfo
            sapi_uv_fs_access
            sapi_uv_fs_chmod
            sapi_uv_fs_chown
            sapi_uv_fs_close
            sapi_uv_fs_closedir
            sapi_uv_fs_copyfile
            sapi_uv_fs_event_getpath
            sapi_uv_fs_event_init
            sapi_uv_fs_event_start
            sapi_uv_fs_event_stop
            sapi_uv_fs_fchmod
            sapi_uv_fs_fchown
            sapi_uv_fs_fdatasync
            sapi_uv_fs_fstat
            sapi_uv_fs_fsync
            sapi_uv_fs_ftruncate
            sapi_uv_fs_futime
            sapi_uv_fs_get_path
            sapi_uv_fs_get_ptr
            sapi_uv_fs_get_result
            sapi_uv_fs_get_statbuf
            sapi_uv_fs_get_system_error
            sapi_uv_fs_get_type
            sapi_uv_fs_lchown
            sapi_uv_fs_link
            sapi_uv_fs_lstat
            sapi_uv_fs_lutime
            sapi_uv_fs_mkdir
            sapi_uv_fs_mkdtemp
            sapi_uv_fs_mkstemp
            sapi_uv_fs_open
            sapi_uv_fs_opendir
            sapi_uv_fs_poll_getpath
            sapi_uv_fs_poll_init
            sapi_uv_fs_poll_start
            sapi_uv_fs_poll_stop
            sapi_uv_fs_read
            sapi_uv_fs_readdir
            sapi_uv_fs_readlink
            sapi_uv_fs_realpath
            sapi_uv_fs_rename
            sapi_uv_fs_req_cleanup
            sapi_uv_fs_rmdir
            sapi_uv_fs_scandir
            sapi_uv_fs_scandir_next
            sapi_uv_fs_sendfile
            sapi_uv_fs_stat
            sapi_uv_fs_statfs
            sapi_uv_fs_symlink
            sapi_uv_fs_unlink
            sapi_uv_fs_utime
            sapi_uv_fs_write
            sapi_uv_get_constrained_memory
            sapi_uv_get_free_memory
            sapi_uv_get_osfhandle
            sapi_uv_get_process_title
            sapi_uv_get_total_memory
            sapi_uv_getaddrinfo
            sapi_uv_getnameinfo
            sapi_uv_getrusage
            sapi_uv_gettimeofday
            sapi_uv_guess_handle
            sapi_uv_handle_get_data
            sapi_uv_handle_get_loop
            sapi_uv_handle_get_type
            sapi_uv_handle_set_data
            sapi_uv_handle_size
            sapi_uv_handle_type_name
            sapi_uv_has_ref
            sapi_uv_hrtime
            sapi_uv_idle_init
            sapi_uv_idle_start
            sapi_uv_idle_stop
            sapi_uv_if_indextoiid
            sapi_uv_if_indextoname
            sapi_uv_inet_ntop
            sapi_uv_inet_pton
            sapi_uv_interface_addresses
            sapi_uv_ip4_addr
            sapi_uv_ip4_name
            sapi_uv_ip6_addr
            sapi_uv_ip6_name
            sapi_uv_is_active
            sapi_uv_is_closing
            sapi_uv_is_readable
            sapi_uv_is_writable
            sapi_uv_key_create
            sapi_uv_key_delete
            sapi_uv_key_get
            sapi_uv_key_set
            sapi_uv_kill
            sapi_uv_library_shutdown
            sapi_uv_listen
            sapi_uv_loadavg
            sapi_uv_loop_alive
            sapi_uv_loop_close
            sapi_uv_loop_configure
            sapi_uv_loop_configure_int
            sapi_uv_loop_delete
            sapi_uv_loop_fork
            sapi_uv_loop_get_data
            sapi_uv_loop_init
            sapi_uv_loop_new
            sapi_uv_loop_set_data
            sapi_uv_loop_size
            sapi_uv_metrics_idle_time
            sapi_uv_mutex_destroy
            sapi_uv_mutex_init
            sapi_uv_mutex_init_recursive
            sapi_uv_mutex_lock
            sapi_uv_mutex_trylock
            sapi_uv_mutex_unlock
            sapi_uv_now
            sapi_uv_once
            sapi_uv_open_osfhandle
            sapi_uv_os_environ
            sapi_uv_os_free_environ
            sapi_uv_os_free_passwd
            sapi_uv_os_get_passwd
            sapi_uv_os_getenv
            sapi_uv_os_gethostname
            sapi_uv_os_getpid
            sapi_uv_os_getppid
            sapi_uv_os_getpriority
            sapi_uv_os_homedir
            sapi_uv_os_setenv
            sapi_uv_os_setpriority
            sapi_uv_os_tmpdir
            sapi_uv_os_uname
            sapi_uv_os_unsetenv
            sapi_uv_pipe_bind
            sapi_uv_pipe_chmod
            sapi_uv_pipe_connect
            sapi_uv_pipe_getpeername
            sapi_uv_pipe_getsockname
            sapi_uv_pipe_init
            sapi_uv_pipe_open
            sapi_uv_pipe_pending_count
            sapi_uv_pipe_pending_instances
            sapi_uv_pipe_pending_type
            sapi_uv_poll_init
            sapi_uv_poll_init_socket
            sapi_uv_poll_start
            sapi_uv_poll_stop
            sapi_uv_prepare_init
            sapi_uv_prepare_start
            sapi_uv_prepare_stop
            sapi_uv_print_active_handles
            sapi_uv_print_all_handles
            sapi_uv_process_get_pid
            sapi_uv_process_kill
            sapi_uv_queue_work
            sapi_uv_random
            sapi_uv_read_start
            sapi_uv_read_stop
            sapi_uv_recv_buffer_size
            sapi_uv_ref
            sapi_uv_replace_allocator
            sapi_uv_req_get_data
            sapi_uv_req_get_type
            sapi_uv_req_set_data
            sapi_uv_req_size
            sapi_uv_req_type_name
            sapi_uv_resident_set_memory
            sapi_uv_run
            sapi_uv_rwlock_destroy
            sapi_uv_rwlock_init
            sapi_uv_rwlock_rdlock
            sapi_uv_rwlock_rdunlock
            sapi_uv_rwlock_tryrdlock
            sapi_uv_rwlock_trywrlock
            sapi_uv_rwlock_wrlock
            sapi_uv_rwlock_wrunlock
            sapi_uv_sem_destroy
            sapi_uv_sem_init
            sapi_uv_sem_post
            sapi_uv_sem_trywait
            sapi_uv_sem_wait
            sapi_uv_send_buffer_size
            sapi_uv_set_process_title
            sapi_uv_setup_args
            sapi_uv_shutdown
            sapi_uv_signal_init
            sapi_uv_signal_start
            sapi_uv_signal_start_oneshot
            sapi_uv_signal_stop
            sapi_uv_sleep
            sapi_uv_spawn
            sapi_uv_stop
            sapi_uv_stream_get_write_queue_size
            sapi_uv_stream_set_blocking
            sapi_uv_strerror
            sapi_uv_strerror_r
            sapi_uv_tcp_bind
            sapi_uv_tcp_close_reset
            sapi_uv_tcp_connect
            sapi_uv_tcp_getpeername
            sapi_uv_tcp_getsockname
            sapi_uv_tcp_init
            sapi_uv_tcp_init_ex
            sapi_uv_tcp_keepalive
            sapi_uv_tcp_nodelay
            sapi_uv_tcp_open
            sapi_uv_tcp_simultaneous_accepts
            sapi_uv_thread_create
            sapi_uv_thread_create_ex
            sapi_uv_thread_equal
            sapi_uv_thread_join
            sapi_uv_thread_self
            sapi_uv_timer_again
            sapi_uv_timer_get_due_in
            sapi_uv_timer_get_repeat
            sapi_uv_timer_init
            sapi_uv_timer_set_repeat
            sapi_uv_timer_start
            sapi_uv_timer_stop
            sapi_uv_translate_sys_error
            sapi_uv_try_write
            sapi_uv_tty_get_vterm_state
            sapi_uv_tty_get_winsize
            sapi_uv_tty_init
            sapi_uv_tty_reset_mode
            sapi_uv_tty_set_mode
            sapi_uv_tty_set_vterm_state
            sapi_uv_udp_bind
            sapi_uv_udp_connect
            sapi_uv_udp_get_send_queue_count
            sapi_uv_udp_get_send_queue_size
            sapi_uv_udp_getpeername
            sapi_uv_udp_getsockname
            sapi_uv_udp_init
            sapi_uv_udp_init_ex
            sapi_uv_udp_open
            sapi_uv_udp_recv_start
            sapi_uv_udp_recv_stop
            sapi_uv_udp_send
            sapi_uv_udp_set_broadcast
            sapi_uv_udp_set_membership
            sapi_uv_udp_set_multicast_interface
            sapi_uv_udp_set_multicast_loop
            sapi_uv_udp_set_multicast_ttl
            sapi_uv_udp_set_source_membership
            sapi_uv_udp_set_ttl
            sapi_uv_udp_try_send
            sapi_uv_udp_using_recvmmsg
            sapi_uv_unref
            sapi_uv_update_time
            sapi_uv_uptime
            sapi_uv_version
            sapi_uv_version_string
            sapi_uv_walk
            sapi_uv_write
            sapi_uv_write2

  INPUTS "${CMAKE_BINARY_DIR}/uv_wrapper/uv_wrapper.h"

  LIBRARY uv_wrapper_and_callbacks

  LIBRARY_NAME UV

  NAMESPACE uv
)

# Include generated SAPI header
target_include_directories(uv_sapi INTERFACE
  "${PROJECT_BINARY_DIR}"
)

# Add examples
if (SAPI_UV_ENABLE_EXAMPLES)
  add_subdirectory(examples)
endif()

# Add tests
if (SAPI_UV_ENABLE_TESTS)
  add_subdirectory(tests)
endif()
