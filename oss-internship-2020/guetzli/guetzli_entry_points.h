#pragma once

#include "guetzli/processor.h"
#include "sandboxed_api/lenval_core.h"
#include "sandboxed_api/vars.h"

extern "C" bool ProcessJPEGString(const guetzli::Params* params,
                                  int verbose,
                                  sapi::LenValStruct* in_data, 
                                  sapi::LenValStruct* out_data);

extern "C" bool ProcessRGBData(const guetzli::Params* params,
                                int verbose,
                                sapi::LenValStruct* rgb, 
                                int w, int h,
                                sapi::LenValStruct* out_data);

extern "C" bool ReadPng(sapi::LenValStruct* in_data, 
                        int* xsize, int* ysize,
                        sapi::LenValStruct* rgb_out);

extern "C" bool ReadJpegData(sapi::LenValStruct* in_data, 
                              int mode, int* xsize, int* ysize);

extern "C" double ButteraugliScoreQuality(double quality);

extern "C" bool ReadDataFromFd(int fd, sapi::LenValStruct* out_data);

extern "C" bool WriteDataToFd(int fd, sapi::LenValStruct* data);