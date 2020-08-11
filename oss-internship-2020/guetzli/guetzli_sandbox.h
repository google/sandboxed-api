#pragma once

#include <libgen.h>
#include <syscall.h>

#include "guetzli_sapi.sapi.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/policybuilder.h"
#include "sandboxed_api/util/flag.h"

namespace guetzli {
namespace sandbox {

class GuetzliSapiSandbox : public GuetzliSandbox {
  public:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {

      return sandbox2::PolicyBuilder()
        .AllowStaticStartup()
        .AllowRead()
        .AllowSystemMalloc()
        .AllowWrite()
        .AllowExit()
        .AllowStat()
        .AllowSyscalls({
          __NR_futex,
          __NR_close,
          __NR_recvmsg // Seems like this one needed to work with remote file descriptors
        })
        .BuildOrDie();
    }
};

} // namespace sandbox
} // namespace guetzli