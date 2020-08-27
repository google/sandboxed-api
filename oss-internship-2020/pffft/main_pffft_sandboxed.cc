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

#include <glog/logging.h>
#include <stdlib.h>
#include <string.h>
#include <sys/times.h>
#include <syscall.h>
#include <time.h>

#include <cassert>
#include <cmath>
#include <cstdio>

#include "pffft_sapi.sapi.h"
#include "sandboxed_api/util/flag.h"
#include "sandboxed_api/vars.h"

ABSL_DECLARE_FLAG(string, sandbox2_danger_danger_permit_all);
ABSL_DECLARE_FLAG(string, sandbox2_danger_danger_permit_all_and_log);

class PffftSapiSandbox : public PffftSandbox {
 public:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) {
    return sandbox2::PolicyBuilder()
        .AllowStaticStartup()
        .AllowOpen()
        .AllowRead()
        .AllowWrite()
        .AllowSystemMalloc()
        .AllowExit()
        .AllowSyscalls({
            __NR_futex,
            __NR_close,
            __NR_getrusage,
        })
        .BuildOrDie();
  }
};

double UclockSec() { return static_cast<double>(clock()) / CLOCKS_PER_SEC; }

int array_output_format = 0;

void ShowOutput(const char* name, int n, int cplx, float flops, float t0,
                float t1, int max_iter) {
  float mflops = flops / 1e6 / (t1 - t0 + 1e-16);
  if (array_output_format) {
    if (flops != -1) {
      printf("|%9.0f   ", mflops);
    } else
      printf("|      n/a   ");
  } else {
    if (flops != -1) {
      printf("n=%5d, %s %16s : %6.0f MFlops [t=%6.0f ns, %d runs]\n", n,
             (cplx ? "CPLX" : "REAL"), name, mflops,
             (t1 - t0) / 2 / max_iter * 1e9, max_iter);
    }
  }
  fflush(stdout);
}

absl::Status PffftMain() {
  PffftSapiSandbox sandbox;
  SAPI_RETURN_IF_ERROR(sandbox.Init());

  return absl::OkStatus();
}

int main(int argc, char* argv[]) {
  // Initialize Google's logging library.
  google::InitGoogleLogging(argv[0]);

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // kTransformSizes is a vector keeping the values by which iterates n, its value
  // representing the input length. More concrete, n is the number of
  // data points the caclulus is up to (determinating its accuracy).
  // To show the performance of Fast-Fourier Transformations the program is
  // testing for various values of n.
  constexpr int kTransformSizes[] = {64,    96,     128,        160,         192,     256,
                   384,   5 * 96, 512,        5 * 128,     3 * 256, 800,
                   1024,  2048,   2400,       4096,        8192,    9 * 1024,
                   16384, 32768};

  LOG(INFO) << "Initializing sandbox...\n";

  PffftSapiSandbox sandbox;
  absl::Status init_status = sandbox.Init();

  if (absl::Status status = PffftMain(); !status.ok()) {
    LOG(ERROR) << "Initialization failed: " << status.ToString();
    return EXIT_FAILURE;
  }

  LOG(INFO) << "Initialization: " << init_status.ToString();

  PffftApi api(&sandbox);
  int cplx = 0;

  do {
    for (int n : kTransformSizes) {
      const int n_float = n * (cplx ? 2 : 1);
      int n_bytes = n_float * sizeof(float);

      std::vector<float> work(2 * n_float + 15, 0.0);
      sapi::v::Array<float> work_array(&work[0], work.size());

      float x[n_bytes], y[n_bytes], z[n_bytes];
      sapi::v::Array<float> x_array(x, n_bytes), y_array(y, n_bytes), z_array(z, n_bytes);

      double t0;
      double t1;
      double flops;

      int k;
      int max_iter = 5120000 / n * 4;

      for (k = 0; k < n_float; ++k) {
        x[k] = 0;
      }

      // FFTPack benchmark
      {
        // SIMD_SZ == 4 (returning value of pffft_simd_size())
        int max_iter_ = max_iter / 4;

        if (max_iter_ == 0) max_iter_ = 1;
        if (cplx) {
          api.cffti(n, work_array.PtrBoth()).IgnoreError();
        } else {
          api.rffti(n, work_array.PtrBoth()).IgnoreError();
        }
        t0 = UclockSec();

        for (int iter = 0; iter < max_iter_; ++iter) {
          if (cplx) {
            api.cfftf(n, x_array.PtrBoth(), work_array.PtrBoth()).IgnoreError();
            api.cfftb(n, x_array.PtrBoth(), work_array.PtrBoth()).IgnoreError();
          } else {
            api.rfftf(n, x_array.PtrBoth(), work_array.PtrBoth()).IgnoreError();
            api.rfftb(n, x_array.PtrBoth(), work_array.PtrBoth()).IgnoreError();
          }
        }
        t1 = UclockSec();

        flops =
            (max_iter_ * 2) * ((cplx ? 5 : 2.5) * n * log((double)n) / M_LN2);
        ShowOutput("FFTPack", n, cplx, flops, t0, t1, max_iter_);
      }
      
      // PFFFT benchmark
      {
        sapi::StatusOr<PFFFT_Setup*> s =
            api.pffft_new_setup(n, cplx ? PFFFT_COMPLEX : PFFFT_REAL);

        LOG(INFO) << "Setup status is: " << s.status().ToString();

        if (!s.ok()) {
          printf("Sandbox failed.\n");
          return EXIT_FAILURE;
        }

        sapi::v::RemotePtr s_reg(s.value());

        t0 = UclockSec();
        for (int iter = 0; iter < max_iter; ++iter) {
          api.pffft_transform(&s_reg, x_array.PtrBoth(), z_array.PtrBoth(),
                              y_array.PtrBoth(), PFFFT_FORWARD)
              .IgnoreError();
          api.pffft_transform(&s_reg, x_array.PtrBoth(), z_array.PtrBoth(),
                              y_array.PtrBoth(), PFFFT_FORWARD)
              .IgnoreError();
        }

        t1 = UclockSec();
        api.pffft_destroy_setup(&s_reg).IgnoreError();

        flops =
            (max_iter * 2) * ((cplx ? 5 : 2.5) * n * log((double)n) / M_LN2);
        ShowOutput("PFFFT", n, cplx, flops, t0, t1, max_iter);

        LOG(INFO) << "n = " << n << " SUCCESSFULLY";
      }
    }

    cplx = !cplx;
  } while (cplx);

  return EXIT_SUCCESS;
}