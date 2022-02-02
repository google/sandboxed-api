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

#ifndef CONTRIB_JSONNET_JSONNET_HELPER_H_
#define CONTRIB_JSONNET_JSONNET_HELPER_H_

extern "C" {
#include <libjsonnet.h>
#include <libjsonnet_fmt.h>
}

#include "jsonnet/cmd/utils.h"  // NOLINT(build/include)

extern "C" {

struct JsonnetVm* c_jsonnet_make(void);

void c_jsonnet_destroy(struct JsonnetVm* vm);

char* c_jsonnet_evaluate_snippet(struct JsonnetVm* vm, const char* filename,
                                 char* snippet, int* error);

char* c_jsonnet_evaluate_snippet_multi(struct JsonnetVm* vm,
                                       const char* filename,
                                       const char* snippet, int* error);

char* c_jsonnet_evaluate_snippet_stream(struct JsonnetVm* vm,
                                        const char* filename,
                                        const char* snippet, int* error);

char* c_read_input(bool filename_is_code, const char* filename);

void c_free_input(char* input);

bool c_write_output_file(const char* output, const char* output_file);

char* c_jsonnet_realloc(struct JsonnetVm* vm, char* str, size_t sz);

bool c_write_multi_output_files(char* output, char* output_dir,
                                bool show_output_file_names);

bool write_multi_output_files(char* output, const std::string& output_dir,
                              bool show_output_file_names);

bool c_write_output_stream(char* output, char* output_file);

bool write_output_stream(char* output, const std::string& output_file);

char* c_jsonnet_fmt_snippet(struct JsonnetVm* vm, const char* filename,
                            const char* snippet, int* error);
}

#endif  // CONTRIB_JSONNET_JSONNET_HELPER_H_
