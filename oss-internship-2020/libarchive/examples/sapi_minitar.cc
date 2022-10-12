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

#include "absl/status/status.h"
#include "sandboxed_api/util/path.h"
#include "sandboxed_api/util/status_macros.h"

absl::Status CreateArchive(const char* initial_filename, int compress,
                           const char** argv, bool verbose) {
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
  SAPI_RETURN_IF_ERROR(sandbox.Init());
  LibarchiveApi api(&sandbox);

  SAPI_ASSIGN_OR_RETURN(archive * ret_archive, api.archive_write_new());
  if (ret_archive == nullptr) {
    return absl::FailedPreconditionError("Failed to create write archive");
  }

  // Treat the pointer as remote. There is no need to copy the data
  // to the client process.
  sapi::v::RemotePtr a(ret_archive);

  int rc;
  std::string msg;

  //   switch (compress) {
  //     case 'j':
  //     case 'y':
  if (compress == 'j' || compress == 'y') {
    SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_add_filter_bzip2(&a));
    if (rc != ARCHIVE_OK) {
      return absl::FailedPreconditionError(
          "Unexpected result from write_add_filter_bzip2 call");
    }
    //   break;
  } else if (compress == 'Z') {
    // case 'Z':
    SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_add_filter_compress(&a));
    if (rc != ARCHIVE_OK) {
      return absl::FailedPreconditionError(
          "Unexpected result from write_add_filter_compress call");
    }
    //   break;
  } else if (compress == 'z') {
    // case 'z':
    SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_add_filter_gzip(&a));
    if (rc != ARCHIVE_OK) {
      return absl::FailedPreconditionError(
          "Unexpected result from write_add_filter_gzip call");
    }
    //   break;
  } else {
    // default:
    SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_add_filter_none(&a));
    if (rc != ARCHIVE_OK) {
      return absl::FailedPreconditionError(
          "Unexpected result from write_add_filter_none call");
    }
    //   break;
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_set_format_ustar(&a));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from write_set_format_ustar call");
  }

  const char* filename_ptr = filename.data();
  if (filename_ptr != nullptr && strcmp(filename_ptr, "-") == 0) {
    filename_ptr = nullptr;
  }

  SAPI_ASSIGN_OR_RETURN(rc,
                        api.archive_write_open_filename(
                            &a, sapi::v::ConstCStr(filename_ptr).PtrBefore()));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from write_open_filename call");
  }

  int file_idx = 0;

  // We can directly use the vectors defined before.
  for (int file_idx = 0; file_idx < absolute_paths.size(); ++file_idx) {
    SAPI_ASSIGN_OR_RETURN(ret_archive, api.archive_read_disk_new());
    if (ret_archive == nullptr) {
      return absl::FailedPreconditionError(
          "Failed to create read_disk archive");
    }

    sapi::v::RemotePtr disk(ret_archive);

    SAPI_ASSIGN_OR_RETURN(rc, api.archive_read_disk_set_standard_lookup(&disk));
    if (rc != ARCHIVE_OK) {
      return absl::FailedPreconditionError(
          "Unexpected result from read_disk_set_standard_lookup call");
    }

    // We use the absolute path first.
    SAPI_ASSIGN_OR_RETURN(
        rc,
        api.archive_read_disk_open(
            &disk,
            sapi::v::ConstCStr(absolute_paths[file_idx].c_str()).PtrBefore()));
    if (rc != ARCHIVE_OK) {
      SAPI_ASSIGN_OR_RETURN(msg, CheckStatusAndGetString(
                                     api.archive_error_string(&disk), sandbox));
      return absl::FailedPreconditionError(msg);
    }

    while (true) {
      archive_entry* ret_archive_entry;
      SAPI_ASSIGN_OR_RETURN(ret_archive_entry, api.archive_entry_new());

      if (ret_archive_entry == nullptr) {
        return absl::FailedPreconditionError("Failed to create archive_entry");
      }

      sapi::v::RemotePtr entry(ret_archive_entry);

      SAPI_ASSIGN_OR_RETURN(rc, api.archive_read_next_header2(&disk, &entry));

      if (rc == ARCHIVE_EOF) {
        break;
      }

      if (rc != ARCHIVE_OK) {
        SAPI_ASSIGN_OR_RETURN(
            msg,
            CheckStatusAndGetString(api.archive_error_string(&disk), sandbox));
        return absl::FailedPreconditionError(msg);
      }

      SAPI_ASSIGN_OR_RETURN(rc, api.archive_read_disk_descend(&disk));
      if (rc != ARCHIVE_OK) {
        return absl::FailedPreconditionError("read_disk_descend call failed");
      }

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
      SAPI_ASSIGN_OR_RETURN(
          std::string path_name,
          CheckStatusAndGetString(api.archive_entry_pathname(&entry), sandbox));

      path_name.replace(path_name.begin(),
                        path_name.begin() + absolute_paths[file_idx].length(),
                        relative_paths[file_idx]);

      // On top of those changes, we need to remove leading '/' characters
      // and also remove everything up to the last occurrence of '../'.
      size_t found = path_name.find_first_not_of("/");
      if (found != std::string::npos) {
        path_name.erase(path_name.begin(), path_name.begin() + found);
      }

      // Search either for the last '/../' or check if
      // the path has '../' in the beginning.
      found = path_name.rfind("/../");
      if (found != std::string::npos) {
        path_name = path_name.substr(found + 4);
      } else if (path_name.substr(0, 3) == "../") {
        path_name = path_name.substr(3);
      }

      SAPI_RETURN_IF_ERROR(api.archive_entry_set_pathname(
          &entry, sapi::v::ConstCStr(path_name.c_str()).PtrBefore()));

      if (verbose) {
        SAPI_ASSIGN_OR_RETURN(
            msg, CheckStatusAndGetString(api.archive_entry_pathname(&entry),
                                         sandbox));
        std::cout << msg << std::endl;
      }

      SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_header(&a, &entry));

      if (rc < ARCHIVE_OK) {
        SAPI_ASSIGN_OR_RETURN(msg, CheckStatusAndGetString(
                                       api.archive_error_string(&a), sandbox));
        std::cout << msg << std::endl;
      }
      if (rc == ARCHIVE_FATAL) {
        return absl::FailedPreconditionError(
            "Unexpected result from write_header call");
      }

      // In the following section, the calls (read, archive_write_data) are done
      // on the sandboxed process since we do not need to transfer the data in
      // the client process.
      if (rc > ARCHIVE_FAILED) {
        SAPI_ASSIGN_OR_RETURN(
            msg, CheckStatusAndGetString(api.archive_entry_sourcepath(&entry),
                                         sandbox));
        int fd = open(msg.c_str(), O_RDONLY);
        if (fd < 0) {
          return absl::FailedPreconditionError("Could not open file");
        }

        sapi::v::Fd sapi_fd(fd);
        sapi::v::Int read_ret;
        sapi::v::Array<char> buff(kBuffSize);
        sapi::v::UInt ssize(kBuffSize);

        // We allocate the buffer remotely and then we can simply use the
        // remote pointer(with PtrNone).
        // This allows us to keep the data in the remote process without always
        // transferring the memory.
        SAPI_RETURN_IF_ERROR(sandbox.Allocate(&buff, true));

        // We can use sapi methods that help us with file descriptors.
        SAPI_RETURN_IF_ERROR(sandbox.TransferToSandboxee(&sapi_fd));

        SAPI_RETURN_IF_ERROR(
            sandbox.Call("read", &read_ret, &sapi_fd, buff.PtrNone(), &ssize));

        while (read_ret.GetValue() > 0) {
          SAPI_ASSIGN_OR_RETURN(
              rc,
              api.archive_write_data(&a, buff.PtrNone(), read_ret.GetValue()));

          SAPI_RETURN_IF_ERROR(sandbox.Call("read", &read_ret, &sapi_fd,
                                            buff.PtrNone(), &ssize));
        }
        // sapi_fd variable goes out of scope here so both the local and the
        // remote file descriptors are closed.
      }
      SAPI_RETURN_IF_ERROR(api.archive_entry_free(&entry));
    }

    SAPI_ASSIGN_OR_RETURN(rc, api.archive_read_close(&disk));
    if (rc != ARCHIVE_OK) {
      return absl::FailedPreconditionError(
          "Unexpected result from read_close call");
    }

    SAPI_ASSIGN_OR_RETURN(rc, api.archive_read_free(&disk));
    if (rc != ARCHIVE_OK) {
      return absl::FailedPreconditionError(
          "Unexpected result from read_free call");
    }
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_close(&a));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from write_close call");
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_free(&a));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from write_free call");
  }

  return absl::OkStatus();
}

absl::Status ExtractArchive(const char* filename, int do_extract, int flags,
                            bool verbose) {
  std::string tmp_dir;
  if (do_extract) {
    SAPI_ASSIGN_OR_RETURN(tmp_dir, CreateTempDirAtCWD());
  }

  // We can use a struct like this in order to delete the temporary
  // directory that was created earlier whenever the function ends.
  struct ExtractTempDirectoryCleanup {
    ExtractTempDirectoryCleanup(const std::string& dir) : dir_(dir) {}
    ~ExtractTempDirectoryCleanup() {
      sandbox2::file_util::fileops::DeleteRecursively(dir_);
    }

   private:
    std::string dir_;
  };

  // We should only delete it if the do_extract flag is true which
  // means that this struct is instantiated only in that case.
  auto cleanup_ptr =
      do_extract ? std::make_unique<ExtractTempDirectoryCleanup>(tmp_dir)
                 : nullptr;

  std::string filename_absolute = MakeAbsolutePathAtCWD(filename);

  // Initialize sandbox and api objects.
  SapiLibarchiveSandboxExtract sandbox(filename_absolute, do_extract, tmp_dir);
  SAPI_RETURN_IF_ERROR(sandbox.Init());
  LibarchiveApi api(&sandbox);

  SAPI_ASSIGN_OR_RETURN(archive * ret_archive, api.archive_read_new());
  if (ret_archive == nullptr) {
    return absl::FailedPreconditionError("Failed to create read archive");
  }

  sapi::v::RemotePtr a(ret_archive);

  SAPI_ASSIGN_OR_RETURN(ret_archive, api.archive_write_disk_new());
  if (ret_archive == nullptr) {
    return absl::FailedPreconditionError("Failed to create write disk archive");
  }

  sapi::v::RemotePtr ext(ret_archive);

  int rc;
  std::string msg;

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_disk_set_options(&ext, flags));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from write_disk_set_options call");
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_read_support_filter_bzip2(&a));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from read_support_filter_bzip2 call");
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_read_support_filter_gzip(&a));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from read_suppport_filter_gzip call");
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_read_support_filter_compress(&a));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from read_support_filter_compress call");
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_read_support_format_tar(&a));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result fromread_support_format_tar call");
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_read_support_format_cpio(&a));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from read_support_format_tar call");
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_disk_set_standard_lookup(&ext));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from write_disk_set_standard_lookup call");
  }

  const char* filename_ptr = filename_absolute.c_str();
  if (filename_ptr != nullptr && strcmp(filename_ptr, "-") == 0) {
    filename_ptr = nullptr;
  }

  // The entries are saved with a relative path so they are all created
  // relative to the current working directory.
  SAPI_ASSIGN_OR_RETURN(
      rc, api.archive_read_open_filename(
              &a, sapi::v::ConstCStr(filename_ptr).PtrBefore(), kBlockSize));
  if (rc != ARCHIVE_OK) {
    SAPI_ASSIGN_OR_RETURN(
        msg, CheckStatusAndGetString(api.archive_error_string(&a), sandbox));
    return absl::FailedPreconditionError(msg);
  }

  while (true) {
    sapi::v::IntBase<archive_entry*> entry_ptr_tmp(0);

    SAPI_ASSIGN_OR_RETURN(
        rc, api.archive_read_next_header(&a, entry_ptr_tmp.PtrAfter()));

    if (rc == ARCHIVE_EOF) {
      break;
    }

    if (rc != ARCHIVE_OK) {
      SAPI_ASSIGN_OR_RETURN(
          msg, CheckStatusAndGetString(api.archive_error_string(&a), sandbox));
      return absl::FailedPreconditionError(msg);
    }

    sapi::v::RemotePtr entry(entry_ptr_tmp.GetValue());

    if (verbose && do_extract) {
      std::cout << "x ";
    }

    if (verbose || !do_extract) {
      SAPI_ASSIGN_OR_RETURN(
          msg,
          CheckStatusAndGetString(api.archive_entry_pathname(&entry), sandbox));
      std::cout << msg << std::endl;
    }

    if (do_extract) {
      SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_header(&ext, &entry));

      if (rc != ARCHIVE_OK) {
        SAPI_ASSIGN_OR_RETURN(msg, CheckStatusAndGetString(
                                       api.archive_error_string(&a), sandbox));
        std::cout << msg << std::endl;
      } else {
        SAPI_ASSIGN_OR_RETURN(rc, CopyData(&a, &ext, api, sandbox));
        if (rc != ARCHIVE_OK) {
          return absl::FailedPreconditionError(
              "Failed to copy data between archive structs.");
        }
      }
    }
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_read_close(&a));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected value from read_close call");
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_read_free(&a));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from read_free call");
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_close(&ext));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from write_close call");
  }

  SAPI_ASSIGN_OR_RETURN(rc, api.archive_write_free(&ext));
  if (rc != ARCHIVE_OK) {
    return absl::FailedPreconditionError(
        "Unexpected result from write_free call");
  }

  return absl::OkStatus();
}

absl::StatusOr<int> CopyData(sapi::v::RemotePtr* ar, sapi::v::RemotePtr* aw,
                             LibarchiveApi& api,
                             SapiLibarchiveSandboxExtract& sandbox) {
  int rc;
  std::string msg;

  sapi::v::IntBase<archive_entry*> buff_ptr_tmp(0);
  sapi::v::ULLong size;
  sapi::v::SLLong offset;

  while (true) {
    SAPI_ASSIGN_OR_RETURN(
        rc, api.archive_read_data_block(ar, buff_ptr_tmp.PtrAfter(),
                                        size.PtrAfter(), offset.PtrAfter()));

    if (rc == ARCHIVE_EOF) {
      return ARCHIVE_OK;
    }
    if (rc != ARCHIVE_OK) {
      SAPI_ASSIGN_OR_RETURN(
          msg, CheckStatusAndGetString(api.archive_error_string(ar), sandbox));
      std::cout << msg << std::endl;
      return rc;
    }

    sapi::v::RemotePtr buff(buff_ptr_tmp.GetValue());

    SAPI_ASSIGN_OR_RETURN(
        rc, api.archive_write_data_block(aw, &buff, size.GetValue(),
                                         offset.GetValue()));

    if (rc != ARCHIVE_OK) {
      SAPI_ASSIGN_OR_RETURN(
          msg, CheckStatusAndGetString(api.archive_error_string(ar), sandbox));
      std::cout << msg << std::endl;
      return rc;
    }
  }
}

std::string MakeAbsolutePathAtCWD(const std::string& path) {
  std::string result = sandbox2::file_util::fileops::MakeAbsolute(
      path, sandbox2::file_util::fileops::GetCWD());
  CHECK(result != "") << "Could not create absolute path for: " << path;
  return sandbox2::file::CleanPath(result);
}

absl::StatusOr<std::string> CheckStatusAndGetString(
    const absl::StatusOr<char*>& status, LibarchiveSandbox& sandbox) {
  SAPI_ASSIGN_OR_RETURN(char* str, status);
  if (str == nullptr) {
    return absl::FailedPreconditionError("Could not get string from archive");
  }
  return sandbox.GetCString(sapi::v::RemotePtr(str));
}

absl::StatusOr<std::string> CreateTempDirAtCWD() {
  std::string cwd = sandbox2::file_util::fileops::GetCWD();
  CHECK(!cwd.empty()) << "Could not get current working directory";
  cwd.append("/");

  SAPI_ASSIGN_OR_RETURN(std::string result, sandbox2::CreateTempDir(cwd));
  return result;
}
