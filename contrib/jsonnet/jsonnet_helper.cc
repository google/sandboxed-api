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

#include "contrib/jsonnet/jsonnet_helper.h"

#include <cstring>

struct JsonnetVm* c_jsonnet_make(void) {
  return jsonnet_make();
}

void c_jsonnet_destroy(struct JsonnetVm* vm) { return jsonnet_destroy(vm); }

char* c_jsonnet_evaluate_snippet(struct JsonnetVm* vm, const char* filename,
                                 char* snippet, int* error) {
  return jsonnet_evaluate_snippet(vm, filename, snippet, error);
}

char* c_jsonnet_evaluate_snippet_multi(struct JsonnetVm* vm,
                                       const char* filename,
                                       const char* snippet, int* error) {
  return jsonnet_evaluate_snippet_multi(vm, filename, snippet, error);
}

char* c_jsonnet_evaluate_snippet_stream(struct JsonnetVm* vm,
                                        const char* filename,
                                        const char* snippet, int* error) {
  return jsonnet_evaluate_snippet_stream(vm, filename, snippet, error);
}

char* c_read_input(bool filename_is_code, const char* filename) {
  std::string s_filename(filename);
  std::string s_input;
  bool check = read_input(filename_is_code, &s_filename, &s_input);
  char* c_input = strdup(s_input.c_str());
  if (check) return c_input;
  return nullptr;
}

void c_free_input(char* input) { free(input); }

bool c_write_output_file(const char* output, const char* output_file) {
  std::string s_output_file(output_file);
  return write_output_file(output, s_output_file);
}

bool c_write_multi_output_files(char* output, char* output_dir,
                                bool show_output_file_names) {
  std::string s_output_dir(output_dir);
  return write_multi_output_files(output, s_output_dir, show_output_file_names);
}

bool c_write_output_stream(char* output, char* output_file) {
  std::string s_output_file(output_file);
  return write_output_stream(output, s_output_file);
}

char* c_jsonnet_realloc(struct JsonnetVm* vm, char* str, size_t sz) {
  return jsonnet_realloc(vm, str, sz);
}

char* c_jsonnet_fmt_snippet(struct JsonnetVm* vm, const char* filename,
                            const char* snippet, int* error) {
  return jsonnet_fmt_snippet(vm, filename, snippet, error);
}
