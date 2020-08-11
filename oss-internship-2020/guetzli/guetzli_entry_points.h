#pragma once

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