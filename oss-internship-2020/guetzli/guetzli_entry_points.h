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

#ifndef GUETZLI_SANDBOXED_GUETZLI_ENTRY_POINTS_H_
#define GUETZLI_SANDBOXED_GUETZLI_ENTRY_POINTS_H_

#include "guetzli/processor.h"
#include "sandboxed_api/lenval_core.h"
#include "sandboxed_api/vars.h"

struct ProcessingParams {
  int remote_fd = -1;
  int verbose = 0;
  int quality = 0;
  int memlimit_mb = 0;
};

extern "C" bool ProcessJpeg(const ProcessingParams* processing_params,
                            sapi::LenValStruct* output);
extern "C" bool ProcessRgb(const ProcessingParams* processing_params,
                           sapi::LenValStruct* output);
extern "C" bool WriteDataToFd(int fd, sapi::LenValStruct* data);

#endif  // GUETZLI_SANDBOXED_GUETZLI_ENTRY_POINTS_H_
