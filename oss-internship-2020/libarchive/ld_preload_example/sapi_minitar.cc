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

#include "sapi_minitar.h"  // NOLINT(build/include)

#include <dlfcn.h>

#include <iostream>
#include <memory>

#include "libarchive_sapi.sapi.h"  // NOLINT(build/include)
#include "sandbox.h"               // NOLINT(build/include)
#include "sandboxed_api/var_array.h"
#include "sandboxed_api/var_ptr.h"

SapiLibarchiveSandboxExtract* sandbox_extract;
LibarchiveApi* api;
char* c_str_tmp = nullptr;

typedef void (*real_extract)(const char*, int, int, int);

void extract(const char* filename, int do_extract, int flags, int verbose) {
  // Here we initialize the sandbox and other objects.
  std::string tmp_dir;
  if (do_extract) {
    tmp_dir = CreateTempDirAtCWD().value();
  }

  std::string filename_absolute = MakeAbsolutePathAtCWD(filename);

  // Initialize sandbox and api objects.
  sandbox_extract =
      new SapiLibarchiveSandboxExtract(filename_absolute, do_extract, tmp_dir);
  CHECK(sandbox_extract->Init().ok()) << "Error during sandbox initialization";
  api = new LibarchiveApi(sandbox_extract);

  // After everything is set up, call the original function (next symbol).

  // TODO getting the mangled name of the function at runtime does not work
  // as intended. At the moment just use the symbol directly.
  const char* y = "_Z7extractPKciii";
  void* x = dlsym(RTLD_NEXT, y);

  CHECK(x != nullptr) << "dlsym call could not find function symbol";
  ((real_extract)x)(filename_absolute.c_str(), do_extract, flags, verbose);

  // clean up
  if (c_str_tmp != nullptr) {
    delete[] c_str_tmp;
  }

  // This is the last function called so we can delete the temporary directory
  // here
  if (do_extract) {
    sandbox2::file_util::fileops::DeleteRecursively(tmp_dir);
  }

  delete api;
  delete sandbox_extract;
}

archive* archive_read_new() {
  archive* ret = api->archive_read_new().value();
  CHECK(ret != nullptr) << "Failed to create archive";
  return ret;
}

archive* archive_write_disk_new() {
  archive* ret = api->archive_write_disk_new().value();
  CHECK(ret != nullptr) << "Failed to create archive";
  return ret;
}

int archive_write_disk_set_options(archive* ext, int flags) {
  sapi::v::RemotePtr ext_ptr(ext);

  return api->archive_write_disk_set_options(&ext_ptr, flags).value();
}

int archive_read_support_filter_bzip2(archive* a) {
  sapi::v::RemotePtr a_ptr(a);
  return api->archive_read_support_filter_bzip2(&a_ptr).value();
}

int archive_read_support_filter_gzip(archive* a) {
  sapi::v::RemotePtr a_ptr(a);
  return api->archive_read_support_filter_gzip(&a_ptr).value();
}

int archive_read_support_filter_compress(archive* a) {
  sapi::v::RemotePtr a_ptr(a);
  return api->archive_read_support_filter_compress(&a_ptr).value();
}
int archive_read_support_format_tar(archive* a) {
  sapi::v::RemotePtr a_ptr(a);
  return api->archive_read_support_format_tar(&a_ptr).value();
}

int archive_read_support_format_cpio(archive* a) {
  sapi::v::RemotePtr a_ptr(a);
  return api->archive_read_support_format_cpio(&a_ptr).value();
}

int archive_write_disk_set_standard_lookup(archive* ext) {
  sapi::v::RemotePtr ext_ptr(ext);

  return api->archive_write_disk_set_standard_lookup(&ext_ptr).value();
}

int archive_read_open_filename(archive* a, const char* _filename,
                               size_t _block_size) {
  sapi::v::RemotePtr a_ptr(a);

  return api
      ->archive_read_open_filename(
          &a_ptr, sapi::v::ConstCStr(_filename).PtrBefore(), _block_size)
      .value();
}

int archive_read_next_header(archive* a, archive_entry** entry) {
  sapi::v::IntBase<archive_entry*> entry_ptr_tmp(0);
  sapi::v::RemotePtr a_ptr(a);
  int rc =
      api->archive_read_next_header(&a_ptr, entry_ptr_tmp.PtrAfter()).value();
  *entry = entry_ptr_tmp.GetValue();
  return rc;
}

// In the following two functions we need to transfer a string from the
// sandboxed process to the client process. However, this string would
// go out of scope after this function so we use a global char * to make
// sure it does not get automatically deleted before it is used.
const char* archive_error_string(archive* a) {
  sapi::v::RemotePtr a_ptr(a);
  char* str = api->archive_error_string(&a_ptr).value();
  CHECK(str != nullptr) << "Could not get error message";

  std::string str_tmp =
      sandbox_extract->GetCString(sapi::v::RemotePtr(str)).value();

  if (c_str_tmp != nullptr) {
    delete[] c_str_tmp;
  }

  c_str_tmp = new char[str_tmp.length() + 1];
  strcpy(c_str_tmp, str_tmp.c_str());  // NOLINT(runtime/printf)

  return c_str_tmp;
}

const char* archive_entry_pathname(archive_entry* entry) {
  sapi::v::RemotePtr entry_ptr(entry);
  char* str = api->archive_entry_pathname(&entry_ptr).value();
  CHECK(str != nullptr) << "Could not get pathname";

  std::string str_tmp =
      sandbox_extract->GetCString(sapi::v::RemotePtr(str)).value();

  if (c_str_tmp != nullptr) {
    delete[] c_str_tmp;
  }

  c_str_tmp = new char[str_tmp.length() + 1];
  strcpy(c_str_tmp, str_tmp.c_str());  // NOLINT(runtime/printf)

  return c_str_tmp;
}

int archive_read_close(archive* a) {
  sapi::v::RemotePtr a_ptr(a);
  return api->archive_read_close(&a_ptr).value();
}

int archive_read_free(archive* a) {
  sapi::v::RemotePtr a_ptr(a);
  return api->archive_read_free(&a_ptr).value();
}

int archive_write_close(archive* a) {
  sapi::v::RemotePtr a_ptr(a);
  return api->archive_write_close(&a_ptr).value();
}

int archive_write_free(archive* a) {
  sapi::v::RemotePtr a_ptr(a);
  return api->archive_write_free(&a_ptr).value();
}

int archive_write_header(archive* a, archive_entry* entry) {
  sapi::v::RemotePtr a_ptr(a), entry_ptr(entry);
  return api->archive_write_header(&a_ptr, &entry_ptr).value();
}

int archive_read_data_block(archive* a, const void** buff, size_t* size,
                            la_int64_t* offset) {
  sapi::v::IntBase<archive_entry*> buff_ptr_tmp(0);
  sapi::v::ULLong size_tmp;
  sapi::v::SLLong offset_tmp;
  sapi::v::RemotePtr a_ptr(a);

  int rv =
      api->archive_read_data_block(&a_ptr, buff_ptr_tmp.PtrAfter(),
                                   size_tmp.PtrAfter(), offset_tmp.PtrAfter())
          .value();
  *buff = buff_ptr_tmp.GetValue();
  *size = size_tmp.GetValue();
  *offset = offset_tmp.GetValue();

  return rv;
}

la_ssize_t archive_write_data_block(archive* a, const void* buff, size_t s,
                                    la_int64_t o) {
  sapi::v::RemotePtr buff_ptr((void*)(buff));
  sapi::v::RemotePtr a_ptr(a);

  return api->archive_write_data_block(&a_ptr, &buff_ptr, s, o).value();
}
