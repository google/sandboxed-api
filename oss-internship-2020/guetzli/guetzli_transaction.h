#pragma once

#include <libgen.h>
#include <syscall.h>

#include "guetzli_sandbox.h"
#include "sandboxed_api/transaction.h"
#include "sandboxed_api/vars.h"

namespace guetzli {
namespace sandbox {

constexpr int kDefaultTransactionRetryCount = 0;
constexpr uint64_t kMpixPixels = 1'000'000;

enum class ImageType {
  JPEG,
  PNG
};

struct TransactionParams {
  int in_fd = -1;
  int out_fd = -1;
  int verbose = 0;
  int quality = 0;
  int memlimit_mb = 0;
};

// Instance of this transaction shouldn't be reused
// Create a new one for each processing operation
class GuetzliTransaction : public sapi::Transaction {
 public:
  GuetzliTransaction(TransactionParams&& params)
      : sapi::Transaction(std::make_unique<GuetzliSapiSandbox>())
      , params_(std::move(params))
      , in_fd_(params_.in_fd)
      , out_fd_(params_.out_fd)
  {
    //TODO: Add retry count as a parameter
    sapi::Transaction::set_retry_count(kDefaultTransactionRetryCount);
    //TODO: Try to use sandbox().set_wall_limit instead of infinite time limit
    sapi::Transaction::SetTimeLimit(0);
  }

 private:
  absl::Status Init() override;
  absl::Status Main() final;

  sapi::StatusOr<ImageType> GetImageTypeFromFd(int fd) const;

  // As guetzli takes roughly 1 minute of CPU per 1 MPix we need to calculate 
  // approximate time for transaction to complete
  time_t CalculateTimeLimitFromImageSize(uint64_t pixels) const;

  const TransactionParams params_;
  sapi::v::Fd in_fd_;
  sapi::v::Fd out_fd_;
  ImageType image_type_ = ImageType::JPEG;
};

} // namespace sandbox
} // namespace guetzli
