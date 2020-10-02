// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sapi_minitar.h"
#include "sandboxed_api/sandbox2/util/path.h"

void create(const char* initial_filename, int compress, const char** argv,
            bool verbose) {
  // We split the filename path into dirname and filename. To the filename we
  // prepend "/output/"" so that it will work with the security policy.
  std::string abs_path = MakeAbsolutePathAtCWD(std::string(initial_filename));
  auto [archive_path, filename_tmp] =
      std::move(sandbox2::file::SplitPath(abs_path));

  std::string filename = sandbox2::file::JoinPath("/output/", filename_tmp);

  std::vector<std::string> absolute_paths;
  sandbox2::util::CharPtrArrToVecString(const_cast<char* const*>(argv),
                                        &absolute_paths);

  std::vector<std::string> relative_paths = absolute_paths;

  std::transform(absolute_paths.begin(), absolute_paths.end(),
                 absolute_paths.begin(), MakeAbsolutePathAtCWD);

  std::transform(relative_paths.begin(), relative_paths.end(),
                 relative_paths.begin(), sandbox2::file::CleanPath);

  // At this point, we have the relative and absolute paths (cleaned) saved
  // in vectors.

  // Initialize sandbox and api objects.
  SapiLibarchiveSandboxCreate sandbox(absolute_paths, archive_path);
  CHECK(sandbox.Init().ok()) << "Error during sandbox initialization";
  LibarchiveApi api(&sandbox);

  absl::StatusOr<archive*> ret = api.archive_write_new();
  CHECK(ret.ok()) << "write_new call failed";
  CHECK(ret.value() != NULL) << "Failed to create write archive";

  // Treat the pointer as remote. There is no need to copy the data
  // to the client process.
  sapi::v::RemotePtr a(ret.value());

  absl::StatusOr<int> ret2;

  switch (compress) {
    case 'j':
    case 'y':
      ret2 = api.archive_write_add_filter_bzip2(&a);
      CHECK(ret2.ok()) << "write_add_filter_bzip2 call failed";
      CHECK(ret2.value() != ARCHIVE_FATAL)
          << "Unexpected result from write_add_filter_bzip2 call";
      break;
    case 'Z':
      ret2 = api.archive_write_add_filter_compress(&a);
      CHECK(ret2.ok()) << "write_add_filter_compress call failed";
      CHECK(ret2.value() != ARCHIVE_FATAL)
          << "Unexpected result from write_add_filter_compress call";
      break;
    case 'z':
      ret2 = api.archive_write_add_filter_gzip(&a);
      CHECK(ret2.ok()) << "write_add_filter_gzip call failed";
      CHECK(ret2.value() != ARCHIVE_FATAL)
          << "Unexpected result from write_add_filter_gzip call";
      break;
    default:
      ret2 = api.archive_write_add_filter_none(&a);
      CHECK(ret2.ok()) << "write_add_filter_none call failed";
      CHECK(ret2.value() != ARCHIVE_FATAL)
          << "Unexpected result from write_add_filter_none call";
      break;
  }

  ret2 = api.archive_write_set_format_ustar(&a);
  CHECK(ret2.ok()) << "write_set_format_ustar call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from write_set_format_ustar call";

  const char* filename_ptr = filename.data();
  if (filename_ptr != NULL && strcmp(filename_ptr, "-") == 0) {
    filename_ptr = NULL;
  }

  ret2 = api.archive_write_open_filename(
      &a, sapi::v::ConstCStr(filename_ptr).PtrBefore());
  CHECK(ret2.ok()) << "write_open_filename call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from write_open_filename call";

  int file_idx = 0;

  // We can directly use the vectors defined before.
  for (int file_idx = 0; file_idx < absolute_paths.size(); ++file_idx) {
    ret = api.archive_read_disk_new();
    CHECK(ret.ok()) << "read_disk_new call failed";
    CHECK(ret.value() != NULL) << "Failed to create read_disk archive";

    sapi::v::RemotePtr disk(ret.value());

    ret2 = api.archive_read_disk_set_standard_lookup(&disk);
    CHECK(ret2.ok()) << "read_disk_set_standard_lookup call failed";
    CHECK(ret2.value() != ARCHIVE_FATAL)
        << "Unexpected result from read_disk_set_standard_lookup call";

    // We use the absolute path first.
    ret2 = api.archive_read_disk_open(
        &disk,
        sapi::v::ConstCStr(absolute_paths[file_idx].c_str()).PtrBefore());
    CHECK(ret2.ok()) << "read_disk_open call failed";
    CHECK(ret2.value() == ARCHIVE_OK)
        << CheckStatusAndGetString(api.archive_error_string(&disk), sandbox);

    for (;;) {
      absl::StatusOr<archive_entry*> ret3;
      ret3 = api.archive_entry_new();

      CHECK(ret3.ok()) << "entry_new call failed";
      CHECK(ret3.value() != NULL) << "Failed to create archive_entry";

      sapi::v::RemotePtr entry(ret3.value());

      ret2 = api.archive_read_next_header2(&disk, &entry);
      CHECK(ret2.ok()) << "read_next_header2 call failed";

      if (ret2.value() == ARCHIVE_EOF) {
        break;
      }

      CHECK(ret2.value() == ARCHIVE_OK)
          << CheckStatusAndGetString(api.archive_error_string(&disk), sandbox);

      ret2 = api.archive_read_disk_descend(&disk);
      CHECK(ret2.ok()) << "read_disk_descend call failed";

      // After using the absolute path before, we now need to add the pathname
      // to the archive entry. This would help store the files by their relative
      // paths(similar to the usual tar command).
      // However, in the case where a directory is added to the archive,
      // all of the files inside of it are added as well so we replace the
      // absolute path prefix with the relative one.
      // Example:
      // we add the folder "test_files" which becomes
      // "/absolute/path/test_files" and the files inside of it will become
      // similar to "/absolute/path/test_files/file1"
      // which we then change to "test_files/file1" so that it is relative.
      std::string path_name =
          CheckStatusAndGetString(api.archive_entry_pathname(&entry), sandbox);

      path_name.replace(path_name.begin(),
                        path_name.begin() + absolute_paths[file_idx].length(),
                        relative_paths[file_idx]);

      // On top of those changes, we need to remove leading '/' characters
      // and also remove everything up to the last occurrence of '../'.
      size_t found = path_name.find_first_not_of("/");
      if (found != std::string::npos) {
        path_name.erase(path_name.begin(), path_name.begin() + found);
      }

      found = path_name.rfind("../");
      if (found != std::string::npos) {
        path_name = path_name.substr(found + 3);
      }

      CHECK(api.archive_entry_set_pathname(
                   &entry, sapi::v::ConstCStr(path_name.c_str()).PtrBefore())
                .ok())
          << "Could not set pathname";

      if (verbose) {
        std::cout << CheckStatusAndGetString(api.archive_entry_pathname(&entry),
                                             sandbox)
                  << std::endl;
      }

      ret2 = api.archive_write_header(&a, &entry);
      CHECK(ret2.ok()) << "write_header call failed";

      if (ret2.value() < ARCHIVE_OK) {
        std::cout << CheckStatusAndGetString(api.archive_error_string(&a),
                                             sandbox)
                  << std::endl;
      }
      CHECK(ret2.value() != ARCHIVE_FATAL)
          << "Unexpected result from write_header call";

      // In the following section, the calls (read, archive_write_data) are done
      // on the sandboxed process since we do not need to transfer the data in
      // the client process.
      if (ret2.value() > ARCHIVE_FAILED) {
        int fd = open(CheckStatusAndGetString(
                          api.archive_entry_sourcepath(&entry), sandbox)
                          .c_str(),
                      O_RDONLY);
        CHECK(fd >= 0) << "Could not open file";

        sapi::v::Fd sapi_fd(fd);
        sapi::v::Int read_ret;
        sapi::v::Array<char> buff(kBuffSize);
        sapi::v::UInt ssize(kBuffSize);

        // We allocate the buffer remotely and then we can simply use the
        // remote pointer(with PtrNone).
        CHECK(sandbox.Allocate(&buff, true).ok())
            << "Could not allocate remote buffer";

        // We can use sapi methods that help us with file descriptors.
        CHECK(sandbox.TransferToSandboxee(&sapi_fd).ok())
            << "Could not transfer file descriptor";

        CHECK(sandbox.Call("read", &read_ret, &sapi_fd, buff.PtrNone(), &ssize)
                  .ok())
            << "Read call failed";

        while (read_ret.GetValue() > 0) {
          CHECK(api.archive_write_data(&a, buff.PtrNone(), read_ret.GetValue())
                    .ok())
              << "write_data call failed";

          CHECK(
              sandbox.Call("read", &read_ret, &sapi_fd, buff.PtrNone(), &ssize)
                  .ok())
              << "Read call failed";
        }
        // sapi_fd variable goes out of scope here so both the local and the
        // remote file descriptors are closed.
      }
      CHECK(api.archive_entry_free(&entry).ok()) << "entry_free call failed";
    }

    ret2 = api.archive_read_close(&disk);
    CHECK(ret2.ok()) << "read_close call failed";
    CHECK(!ret2.value()) << "Unexpected result from read_close call";

    ret2 = api.archive_read_free(&disk);
    CHECK(ret2.ok()) << "read_free call failed";
    CHECK(!ret2.value()) << "Unexpected result from read_free call";
  }

  ret2 = api.archive_write_close(&a);
  CHECK(ret2.ok()) << "write_close call failed";
  CHECK(!ret2.value()) << "Unexpected result from write_close call";

  ret2 = api.archive_write_free(&a);
  CHECK(ret2.ok()) << "write_free call failed";
  CHECK(!ret2.value()) << "Unexpected result from write_free call";
}

void extract(const char* filename, int do_extract, int flags,
             bool verbose) {
  std::string tmp_dir;
  if (do_extract) {
    tmp_dir = CreateTempDirAtCWD();
  }

  // We can use a struct like this in order to delete the temporary
  // directory that was created earlier whenever the function ends.
  struct ExtractTempDirectoryCleanup {
    ExtractTempDirectoryCleanup(const std::string& dir): dir_(dir) {}
    ~ExtractTempDirectoryCleanup() {
      sandbox2::file_util::fileops::DeleteRecursively(dir_);
    }
   private:
    std::string dir_;
  };

  // We should only delete it if the do_extract flag is true which
  // means that this struct is instantiated only in that case.
  std::unique_ptr<ExtractTempDirectoryCleanup> cleanup_ptr;
  if (do_extract) {
    cleanup_ptr = absl::make_unique<ExtractTempDirectoryCleanup>(tmp_dir);
  }

  std::string filename_absolute = MakeAbsolutePathAtCWD(filename);

  // Initialize sandbox and api objects.
  SapiLibarchiveSandboxExtract sandbox(filename_absolute, do_extract, tmp_dir);
  CHECK(sandbox.Init().ok()) << "Error during sandbox initialization";
  LibarchiveApi api(&sandbox);

  absl::StatusOr<archive*> ret = api.archive_read_new();
  CHECK(ret.ok()) << "archive_read_new call failed";
  CHECK(ret.value() != NULL) << "Failed to create read archive";

  sapi::v::RemotePtr a(ret.value());

  ret = api.archive_write_disk_new();
  CHECK(ret.ok()) << "write_disk_new call failed";
  CHECK(ret.value() != NULL) << "Failed to create write disk archive";

  sapi::v::RemotePtr ext(ret.value());

  absl::StatusOr<int> ret2;
  ret2 = api.archive_write_disk_set_options(&ext, flags);
  CHECK(ret2.ok()) << "write_disk_set_options call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from write_disk_set_options call";

  ret2 = api.archive_read_support_filter_bzip2(&a);
  CHECK(ret2.ok()) << "read_support_filter_bzip2 call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from read_support_filter_bzip2 call";

  ret2 = api.archive_read_support_filter_gzip(&a);
  CHECK(ret2.ok()) << "read_suppport_filter_gzip call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from read_suppport_filter_gzip call";

  ret2 = api.archive_read_support_filter_compress(&a);
  CHECK(ret2.ok()) << "read_support_filter_compress call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from read_support_filter_compress call";

  ret2 = api.archive_read_support_format_tar(&a);
  CHECK(ret2.ok()) << "read_support_format_tar call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result fromread_support_format_tar call";

  ret2 = api.archive_read_support_format_cpio(&a);
  CHECK(ret2.ok()) << "read_support_format_cpio call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from read_support_format_tar call";

  ret2 = api.archive_write_disk_set_standard_lookup(&ext);
  CHECK(ret2.ok()) << "write_disk_set_standard_lookup call failed";
  CHECK(ret2.value() != ARCHIVE_FATAL)
      << "Unexpected result from write_disk_set_standard_lookup call";

  const char* filename_ptr = filename_absolute.c_str();
  if (filename_ptr != NULL && strcmp(filename_ptr, "-") == 0) {
    filename_ptr = NULL;
  }

  // The entries are saved with a relative path so they are all created
  // relative to the current working directory.
  ret2 = api.archive_read_open_filename(
      &a, sapi::v::ConstCStr(filename_ptr).PtrBefore(), kBlockSize);
  CHECK(ret2.ok()) << "read_open_filename call failed";
  CHECK(!ret2.value()) << CheckStatusAndGetString(api.archive_error_string(&a),
                                                  sandbox);

  for (;;) {
    sapi::v::IntBase<struct archive_entry*> entry_ptr_tmp(0);

    ret2 = api.archive_read_next_header(&a, entry_ptr_tmp.PtrAfter());
    CHECK(ret2.ok()) << "read_next_header call failed";

    if (ret2.value() == ARCHIVE_EOF) {
      break;
    }

    CHECK(ret2.value() == ARCHIVE_OK)
        << CheckStatusAndGetString(api.archive_error_string(&a), sandbox);

    sapi::v::RemotePtr entry(entry_ptr_tmp.GetValue());

    if (verbose && do_extract) {
      std::cout << "x ";
    }

    if (verbose || !do_extract) {
      std::cout << CheckStatusAndGetString(api.archive_entry_pathname(&entry),
                                           sandbox)
                << std::endl;
    }

    if (do_extract) {
      ret2 = api.archive_write_header(&ext, &entry);
      CHECK(ret2.ok()) << "write_header call failed";

      if (ret2.value() != ARCHIVE_OK) {
        std::cout << CheckStatusAndGetString(api.archive_error_string(&a),
                                             sandbox);
      } else {
        copy_data(&a, &ext, api, sandbox);
      }
    }
  }

  ret2 = api.archive_read_close(&a);
  CHECK(ret2.ok()) << "read_close call failed";
  CHECK(!ret2.value()) << "Unexpected value from read_close call";

  ret2 = api.archive_read_free(&a);
  CHECK(ret2.ok()) << "read_free call failed";
  CHECK(!ret2.value()) << "Unexpected result from read_free call";

  ret2 = api.archive_write_close(&ext);
  CHECK(ret2.ok()) << "write_close call failed";
  CHECK(!ret2.value()) << "Unexpected result from write_close call";

  ret2 = api.archive_write_free(&ext);
  CHECK(ret2.ok()) << "write_free call failed";
  CHECK(!ret2.value()) << "Unexpected result from write_free call";
}

int copy_data(sapi::v::RemotePtr* ar, sapi::v::RemotePtr* aw,
              LibarchiveApi& api, SapiLibarchiveSandboxExtract& sandbox) {
  absl::StatusOr<int> ret;

  sapi::v::IntBase<struct archive_entry*> buff_ptr_tmp(0);
  sapi::v::ULLong size;
  sapi::v::SLLong offset;

  for (;;) {
    ret = api.archive_read_data_block(ar, buff_ptr_tmp.PtrAfter(),
                                      size.PtrAfter(), offset.PtrAfter());
    CHECK(ret.ok()) << "read_data_block call failed";

    if (ret.value() == ARCHIVE_EOF) {
      return ARCHIVE_OK;
    }
    if (ret.value() != ARCHIVE_OK) {
      std::cout << CheckStatusAndGetString(api.archive_error_string(ar),
                                           sandbox);
      return ret.value();
    }

    sapi::v::RemotePtr buff(buff_ptr_tmp.GetValue());

    ret = api.archive_write_data_block(aw, &buff, size.GetValue(),
                                       offset.GetValue());

    CHECK(ret.ok()) << "write_data_block call failed";

    if (ret.value() != ARCHIVE_OK) {
      std::cout << CheckStatusAndGetString(api.archive_error_string(ar),
                                           sandbox);
      return ret.value();
    }
  }
}

std::string MakeAbsolutePathAtCWD(const std::string& path) {
  std::string result = sandbox2::file_util::fileops::MakeAbsolute(
      path, sandbox2::file_util::fileops::GetCWD());
  CHECK(result != "") << "Could not create absolute path for: " << path;
  return sandbox2::file::CleanPath(result);
}

std::string CheckStatusAndGetString(const absl::StatusOr<char*>& status,
                                    LibarchiveSandbox& sandbox) {
  CHECK(status.ok() && status.value() != NULL) << "Could not get error message";

  absl::StatusOr<std::string> ret =
      sandbox.GetCString(sapi::v::RemotePtr(status.value()));
  CHECK(ret.ok()) << "Could not transfer error message";
  return ret.value();
}

std::string CreateTempDirAtCWD() {
  std::string cwd = sandbox2::file_util::fileops::GetCWD();
  CHECK(!cwd.empty()) << "Could not get current working directory";
  cwd.append("/");

  absl::StatusOr<std::string> result = sandbox2::CreateTempDir(cwd);
  CHECK(result.ok()) << "Could not create temporary directory at " << cwd;
  return result.value();
}
