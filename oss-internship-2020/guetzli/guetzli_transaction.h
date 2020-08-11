#pragma once

#include <libgen.h>
#include <syscall.h>

#include "guetzli_sandbox.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/vars.h"

namespace guetzli {
namespace sandbox {

constexpr int kDefaultTransactionRetryCount = 1;
constexpr uint64_t kMpixPixels = 1'000'000;

constexpr int kBytesPerPixel = 350;
constexpr int kLowestMemusageMB = 100; // in MB

struct TransactionParams {
  int in_fd;
  int out_fd;
  int verbose;
  int quality;
  int memlimit_mb;
};

//Add optional time limit/retry count as a constructors arguments
//Use differenet status errors
class GuetzliTransaction : public sapi::Transaction {
 public:
  GuetzliTransaction(TransactionParams&& params)
      : sapi::Transaction(std::make_unique<GuetzliSapiSandbox>())
      , params_(std::move(params))
      , in_fd_(params_.in_fd)
      , out_fd_(params_.out_fd)
  {
    sapi::Transaction::set_retry_count(kDefaultTransactionRetryCount);
    sapi::Transaction::SetTimeLimit(0);
  }

 private:
  absl::Status Init() override;
  absl::Status Main() final;

  absl::Status ProcessPng(GuetzliApi* api, 
                          sapi::v::Struct<Params>* params, 
                          sapi::v::LenVal* input, 
                          sapi::v::LenVal* output) const;

  absl::Status ProcessJpeg(GuetzliApi* api, 
                          sapi::v::Struct<Params>* params, 
                          sapi::v::LenVal* input, 
                          sapi::v::LenVal* output) const;

  // As guetzli takes roughly 1 minute of CPU per 1 MPix we need to calculate 
  // approximate time for transaction to complete
  time_t CalculateTimeLimitFromImageSize(uint64_t pixels) const;

  const TransactionParams params_;
  sapi::v::Fd in_fd_;
  sapi::v::Fd out_fd_;
};

absl::Status GuetzliTransaction::Init() {
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

  return absl::OkStatus();
}

  absl::Status GuetzliTransaction::ProcessPng(GuetzliApi* api, 
                                              sapi::v::Struct<Params>* params, 
                                              sapi::v::LenVal* input, 
                                              sapi::v::LenVal* output) const {
    sapi::v::Int xsize;
    sapi::v::Int ysize;
    sapi::v::LenVal rgb_in(0);

    auto read_result = api->ReadPng(input->PtrBefore(), xsize.PtrBoth(), 
      ysize.PtrBoth(), rgb_in.PtrBoth());
      
    if (!read_result.value_or(false)) {
      return absl::FailedPreconditionError(
        "Error reading PNG data from input file"
      );
    }

    double pixels = static_cast<double>(xsize.GetValue()) * ysize.GetValue();
    if (params_.memlimit_mb != -1
        && (pixels * kBytesPerPixel / (1 << 20) > params_.memlimit_mb
            || params_.memlimit_mb < kLowestMemusageMB)) {
      return absl::FailedPreconditionError(
        "Memory limit would be exceeded"
      );
    }

    auto result = api->ProcessRGBData(params->PtrBefore(), params_.verbose, 
                                    rgb_in.PtrBefore(), xsize.GetValue(), 
                                    ysize.GetValue(), output->PtrBoth());
    if (!result.value_or(false)) {
      return absl::FailedPreconditionError(
        "Guetzli processing failed"
      );
    }

    return absl::OkStatus();
  }

  absl::Status GuetzliTransaction::ProcessJpeg(GuetzliApi* api, 
                                              sapi::v::Struct<Params>* params, 
                                              sapi::v::LenVal* input, 
                                              sapi::v::LenVal* output) const {
    sapi::v::Int xsize;
    sapi::v::Int ysize;
    auto read_result = api->ReadJpegData(input->PtrBefore(), 0, xsize.PtrBoth(), 
      ysize.PtrBoth());

    if (!read_result.value_or(false)) {
      return absl::FailedPreconditionError(
        "Error reading JPG data from input file"
      );
    }

    double pixels = static_cast<double>(xsize.GetValue()) * ysize.GetValue();
    if (params_.memlimit_mb != -1
        && (pixels * kBytesPerPixel / (1 << 20) > params_.memlimit_mb
            || params_.memlimit_mb < kLowestMemusageMB)) {
      return absl::FailedPreconditionError(
        "Memory limit would be exceeded"
      );
    }

    auto result = api->ProcessJPEGString(params->PtrBefore(), params_.verbose, 
      input->PtrBefore(), output->PtrBoth());

    if (!result.value_or(false)) {
      return absl::FailedPreconditionError(
        "Guetzli processing failed"
      );
    }

    return absl::OkStatus();
  }

absl::Status GuetzliTransaction::Main() {
  GuetzliApi api(sandbox());

  sapi::v::LenVal input(0);
  sapi::v::LenVal output(0);
  sapi::v::Struct<Params> params;
  
  auto read_result = api.ReadDataFromFd(in_fd_.GetRemoteFd(), input.PtrBoth());

  if (!read_result.value_or(false)) {
    return absl::FailedPreconditionError(
      "Error reading data inside sandbox"
    );
  }

  auto score_quality_result = api.ButteraugliScoreQuality(params_.quality);

  if (!score_quality_result.ok()) {
    return absl::FailedPreconditionError(
      "Error calculating butteraugli score"
    );
  }

  params.mutable_data()->butteraugli_target = score_quality_result.value();

  static const unsigned char kPNGMagicBytes[] = {
    0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
  };
  
  if (input.GetDataSize() >= 8 &&
    memcmp(input.GetData(), kPNGMagicBytes, sizeof(kPNGMagicBytes)) == 0) {
    auto process_status = ProcessPng(&api, &params, &input, &output);

    if (!process_status.ok()) {
      return process_status;
    }
  } else {
    auto process_status = ProcessJpeg(&api, &params, &input, &output);

    if (!process_status.ok()) {
      return process_status;
    }
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

} // namespace sandbox
} // namespace guetzli
