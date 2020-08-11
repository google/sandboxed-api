#include "guetzli_transaction.h"

#include <iostream>
#include <memory>

namespace guetzli {
namespace sandbox {

absl::Status GuetzliTransaction::Init() {
  // Close remote fd if transaction is repeated
  if (in_fd_.GetRemoteFd() != -1) {
    SAPI_RETURN_IF_ERROR(in_fd_.CloseRemoteFd(sandbox()->GetRpcChannel()));
  }
  if (out_fd_.GetRemoteFd() != -1) {
    SAPI_RETURN_IF_ERROR(out_fd_.CloseRemoteFd(sandbox()->GetRpcChannel()));
  }

  // Reposition back to the beginning of file
  if (lseek(in_fd_.GetValue(), 0, SEEK_CUR) != 0) {
    if (lseek(in_fd_.GetValue(), 0, SEEK_SET) != 0) {
      return absl::FailedPreconditionError(
        "Error returnig cursor to the beginning"
      );
    }
  }

  // Choosing between jpg and png modes
  sapi::StatusOr<ImageType> image_type = GetImageTypeFromFd(in_fd_.GetValue());

  if (!image_type.ok()) {
    return image_type.status();
  }

  image_type_ = image_type.value();

  SAPI_RETURN_IF_ERROR(sandbox()->TransferToSandboxee(&in_fd_));
  SAPI_RETURN_IF_ERROR(sandbox()->TransferToSandboxee(&out_fd_));

  if (in_fd_.GetRemoteFd() < 0) {
    return absl::FailedPreconditionError(
        "Error receiving remote FD: remote input fd is set to -1");
  }
  if (out_fd_.GetRemoteFd() < 0) {
    return absl::FailedPreconditionError(
        "Error receiving remote FD: remote output fd is set to -1");
  }

  in_fd_.OwnLocalFd(false); // FDCloser will close local fd
  out_fd_.OwnLocalFd(false); // FDCloser will close local fd

  return absl::OkStatus();
}

absl::Status GuetzliTransaction::Main() {
  GuetzliApi api(sandbox());
  sapi::v::LenVal output(0);

  sapi::v::Struct<ProcessingParams> processing_params;
  *processing_params.mutable_data() = {in_fd_.GetRemoteFd(), 
                                      params_.verbose, 
                                      params_.quality, 
                                      params_.memlimit_mb
  };

  auto result_status = image_type_ == ImageType::JPEG ? 
    api.ProcessJpeg(processing_params.PtrBefore(), output.PtrBoth()) : 
    api.ProcessRgb(processing_params.PtrBefore(), output.PtrBoth());
  
  if (!result_status.value_or(false)) {
    std::stringstream error_stream;
    error_stream << "Error processing " 
      << (image_type_ == ImageType::JPEG ? "jpeg" : "rgb") << " data" 
      << std::endl; 
    
    return absl::FailedPreconditionError(
      error_stream.str()
    );
  }

  auto write_result = api.WriteDataToFd(out_fd_.GetRemoteFd(), 
    output.PtrBefore());

  if (!write_result.value_or(false)) {
    return absl::FailedPreconditionError(
      "Error writing file inside sandbox"
    );
  }

  return absl::OkStatus();
}

time_t GuetzliTransaction::CalculateTimeLimitFromImageSize(
    uint64_t pixels) const {
  return (pixels / kMpixPixels + 5) * 60;
}

sapi::StatusOr<ImageType> GuetzliTransaction::GetImageTypeFromFd(int fd) const {
  static const unsigned char kPNGMagicBytes[] = {
    0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
  };
  char read_buf[8];

  if (read(fd, read_buf, 8) != 8) {
    return absl::FailedPreconditionError(
      "Error determining type of the input file"
    );
  }

  if (lseek(fd, 0, SEEK_SET) != 0) {
    return absl::FailedPreconditionError(
      "Error returnig cursor to the beginning"
    );
  }

  return memcmp(read_buf, kPNGMagicBytes, sizeof(kPNGMagicBytes)) == 0 ?
      ImageType::PNG : ImageType::JPEG;
}

} // namespace sandbox
} // namespace guetzli