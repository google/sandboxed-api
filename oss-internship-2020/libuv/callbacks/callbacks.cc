// Copyright 2020 Google LLC
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

#include "callbacks.h"  // NOLINT(build/include)

#include <iostream>

size_t g_iterations = 0;
size_t constexpr kMaxIterations = 1'000'000;
static char g_buffer[1024];
static uv_buf_t g_iov;

// Stop the handle if the methods was called kMaxIterations times
void IdleCallback(uv_idle_t* handle) {
  ++g_iterations;
  if (g_iterations > kMaxIterations) {
    std::cout << "IdleCallback was called " << kMaxIterations << " times"
              << std::endl;
    uv_idle_stop(handle);
  }
}

// Called after some chars have been written
// As soon as writing of these bytes is completed, read more
void OnWrite(uv_fs_t* req) {
  if (req->result < 0) {
    std::cerr << "Write error: " << uv_strerror(static_cast<int>(req->result))
              << std::endl;
    return;
  }
  // Start reading more after writing these bytes
  uv_fs_read(uv_default_loop(), &read_req, open_req.result, &g_iov, 1, -1,
             OnRead);
}

// Called after some chars have been read
// As soon as reading of these bytes is completed, write them
void OnRead(uv_fs_t* req) {
  if (req->result < 0) {
    std::cerr << "Read error: " << uv_strerror(req->result) << std::endl;
    return;
  }
  if (req->result == 0) {
    // No more bytes left, close the loop
    uv_fs_t close_req;
    uv_fs_close(uv_default_loop(), &close_req, open_req.result, NULL);
  } else if (req->result > 0) {
    // Start writing after reading some bytes
    g_iov.len = req->result;
    uv_fs_write(uv_default_loop(), &write_req, 1, &g_iov, 1, -1, OnWrite);
  }
}

// Called after the file has been opened
// As soon as opening is completed, read the file
void OnOpen(uv_fs_t* req) {
  if (req != &open_req) {
    std::cerr << "Open error: req != &open_req" << std::endl;
    return;
  }
  if (req->result < 0) {
    std::cerr << "Open error: " << uv_strerror(static_cast<int>(req->result))
              << std::endl;
    return;
  }
  // Initialize uv_buf_t g_buffer
  g_iov = uv_buf_init(g_buffer, sizeof(g_buffer));
  // Start reading after opening
  uv_fs_read(uv_default_loop(), &read_req, req->result, &g_iov, 1, -1, OnRead);
}

// Get the integer pointed by handle->data and increment it by one
// Then close the handle
void TimerCallback(uv_timer_t* handle) {
  int* data = static_cast<int*>(
      uv_handle_get_data(reinterpret_cast<uv_handle_t*>(handle)));
  ++(*data);
  uv_close(reinterpret_cast<uv_handle_t*>(handle), nullptr);
}
