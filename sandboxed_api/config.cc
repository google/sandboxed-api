#include "sandboxed_api/config.h"

#include <cstdlib>

namespace sapi {

bool IsCoverageRun() {
  return getenv("COVERAGE") != nullptr;
}

}  // namespace sapi
