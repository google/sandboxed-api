#ifndef SANDBOXED_API_SANDBOX2_UTIL_POTENTIALLY_BLOCKING_REGION_H_
#define SANDBOXED_API_SANDBOX2_UTIL_POTENTIALLY_BLOCKING_REGION_H_

namespace sandbox2 {
class PotentiallyBlockingRegion {
  public:
  ~PotentiallyBlockingRegion() {
    // Do nothing. Not defaulted to avoid "unused variable" warnings.
  }
};
}  // namespace sandbox2

#endif  // SANDBOXED_API_SANDBOX2_UTIL_POTENTIALLY_BLOCKING_REGION_H_
