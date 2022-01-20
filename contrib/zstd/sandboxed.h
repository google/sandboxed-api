// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 Google LLC
//                Mariusz Zaborski <oshogbo@invisiblethingslab.com>

#ifndef SAPI_LIBZSTD_SANDBOXED_
#define SAPI_LIBZSTD_SANDBOXED_

#include <libgen.h>
#include <syscall.h>

#include "sapi_zstd.sapi.h"
#include "sandboxed_api/util/flag.h"

class ZstdSapiSandbox : public ZstdSandbox {
 public:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder *) override {
    return sandbox2::PolicyBuilder()
        .AllowRead()
        .AllowWrite()
        .AllowSystemMalloc()
        .AllowExit()
        .BuildOrDie();
  }
};

#endif // SAPI_LIBZSTD_SANDBOXED_
