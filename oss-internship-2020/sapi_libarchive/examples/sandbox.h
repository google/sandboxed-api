#ifndef SAPI_LIBARCHIVE_SANDBOX_H
#define SAPI_LIBARCHIVE_SANDBOX_H

#include <syscall.h>
#include "libarchive_sapi.sapi.h"

class SapiLibarchiveSandboxCreate : public LibarchiveSandbox {
 public:
    // TODO
    explicit SapiLibarchiveSandboxCreate() {}
 private:
    std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
    return sandbox2::PolicyBuilder()
        .BuildOrDie();
  }

}



class SapiLibarchiveSandboxExtract : public LibarchiveSandbox {
 public:
    // TODO
    explicit SapiLibarchiveSandboxExtract() {}
 private:
    virtual void ModifyExecutor(sandbox2::Executor* executor) override {
        // TODO create /output/ + chdir here if do_execute
    }
}

#endif  // SAPI_LIBARCHIVE_SANDBOX_H