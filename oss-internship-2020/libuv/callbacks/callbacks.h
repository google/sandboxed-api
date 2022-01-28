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

#ifndef CALLBACKS_H_
#define CALLBACKS_H_

#include <uv.h>

extern "C" {

// idle-basic
void IdleCallback(uv_idle_t* handle);

// uvcat
uv_fs_t open_req;
uv_fs_t read_req;
uv_fs_t write_req;
void OnWrite(uv_fs_t* req);
void OnRead(uv_fs_t* req);
void OnOpen(uv_fs_t* req);

// test_callback
void TimerCallback(uv_timer_t* handle);

}  // extern "C"

#endif  // CALLBACKS_H_
