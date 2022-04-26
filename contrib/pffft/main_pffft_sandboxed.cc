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

#include <syscall.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <glog/logging.h>
#include "pffft_sapi.sapi.h"  // NOLINT(build/include)
#include "sandboxed_api/util/flag.h"
#include "sandboxed_api/vars.h"

ABSL_DECLARE_FLAG(string, sandbox2_danger_danger_permit_all);
ABSL_DECLARE_FLAG(string, sandbox2_danger_danger_permit_all_and_log);

class PffftSapiSandbox : public PffftSandbox {
 public:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(sandbox2::PolicyBuilder*) {
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

// output_format flag determines whether the output shows information in detail
// or not. By default, the flag is set as 0, meaning an elaborate display
// (see ShowOutput method).
static bool ValidateFlag(const char* flagname, int32_t value) {
  if (value >= 0 && value < 32768) {
    return true;
  }

  LOG(ERROR) << "Invalid value for --" << flagname << ".";
  return false;
}

DEFINE_int32(output_format, 0, "Value to specific the output format.");
DEFINE_validator(output_format, &ValidateFlag);

double UclockSec() { return static_cast<double>(clock()) / CLOCKS_PER_SEC; }

void ShowOutput(const char* name, int n, int complex, float flops, float t0,
                float t1, int max_iter) {
  float mflops = flops / 1e6 / (t1 - t0 + 1e-16);
  if (FLAGS_output_format) {
    if (flops != -1) {
      printf("|%9.0f   ", mflops);
    } else {
      printf("|      n/a   ");
    }
  } else if (flops != -1) {
    printf("n=%5d, %s %16s : %6.0f MFlops [t=%6.0f ns, %d runs]\n", n,
           (complex ? "CPLX" : "REAL"), name, mflops,
           (t1 - t0) / 2 / max_iter * 1e9, max_iter);
  }
  fflush(stdout);
}

absl::Status PffftMain() {
  LOG(INFO) << "Initializing sandbox...\n";

  PffftSapiSandbox sandbox;
  SAPI_RETURN_IF_ERROR(sandbox.Init());

  PffftApi api(&sandbox);

  // kTransformSizes is a vector keeping the values by which iterates n, its
  // value representing the input length. More concrete, n is the number of data
  // points the caclulus is up to (determinating its accuracy). To show the
  // performance of Fast-Fourier Transformations the program is testing for
  // various values of n.
  constexpr int kTransformSizes[] = {
      64,      96,  128,  160,  192,  256,  384,  5 * 96,   512,   5 * 128,
      3 * 256, 800, 1024, 2048, 2400, 4096, 8192, 9 * 1024, 16384, 32768};

  for (int complex : {0, 1}) {
    for (int n : kTransformSizes) {
      const int n_float = n * (complex ? 2 : 1);
      int n_bytes = n_float * sizeof(float);

      std::vector<float> work(2 * n_float + 15, 0.0);
      sapi::v::Array<float> work_array(&work[0], work.size());

      std::vector<float> x(n_bytes, 0.0);
      sapi::v::Array<float> x_array(&x[0], x.size());

      std::vector<float> y(n_bytes, 0.0);
      sapi::v::Array<float> y_array(&y[0], y.size());

      std::vector<float> z(n_bytes, 0.0);
      sapi::v::Array<float> z_array(&z[0], z.size());

      double t0;
      double t1;
      double flops;

      int max_iter = 5120000 / n * 4;

      for (int k = 0; k < n_float; ++k) {
        x[k] = 0;
      }

      // FFTPack benchmark
      {
        // SIMD_SZ == 4 (returning value of pffft_simd_size())
        int simd_size_iter = max_iter / 4;

        if (simd_size_iter == 0) simd_size_iter = 1;
        if (complex) {
          SAPI_RETURN_IF_ERROR(api.cffti(n, work_array.PtrBoth()));
        } else {
          SAPI_RETURN_IF_ERROR(api.rffti(n, work_array.PtrBoth()));
        }
        t0 = UclockSec();

        for (int iter = 0; iter < simd_size_iter; ++iter) {
          if (complex) {
            SAPI_RETURN_IF_ERROR(
                api.cfftf(n, x_array.PtrBoth(), work_array.PtrBoth()));
            SAPI_RETURN_IF_ERROR(
                api.cfftb(n, x_array.PtrBoth(), work_array.PtrBoth()));
          } else {
            SAPI_RETURN_IF_ERROR(
                api.rfftf(n, x_array.PtrBoth(), work_array.PtrBoth()));
            SAPI_RETURN_IF_ERROR(
                api.rfftb(n, x_array.PtrBoth(), work_array.PtrBoth()));
          }
        }
        t1 = UclockSec();

        flops = (simd_size_iter * 2) *
                ((complex ? 5 : 2.5) * static_cast<double>(n) *
                 log(static_cast<double>(n)) / M_LN2);
        ShowOutput("FFTPack", n, complex, flops, t0, t1, simd_size_iter);
      }

      // PFFFT benchmark
      {
        SAPI_ASSIGN_OR_RETURN(
            PFFFT_Setup * s,
            api.pffft_new_setup(n, complex ? PFFFT_COMPLEX : PFFFT_REAL));

        sapi::v::RemotePtr s_reg(s);

        t0 = UclockSec();
        for (int iter = 0; iter < max_iter; ++iter) {
          SAPI_RETURN_IF_ERROR(
              api.pffft_transform(&s_reg, x_array.PtrBoth(), z_array.PtrBoth(),
                                  y_array.PtrBoth(), PFFFT_FORWARD));
          SAPI_RETURN_IF_ERROR(
              api.pffft_transform(&s_reg, x_array.PtrBoth(), z_array.PtrBoth(),
                                  y_array.PtrBoth(), PFFFT_FORWARD));
        }

        t1 = UclockSec();
        SAPI_RETURN_IF_ERROR(api.pffft_destroy_setup(&s_reg));

        flops = (max_iter * 2) * ((complex ? 5 : 2.5) * static_cast<double>(n) *
                                  log(static_cast<double>(n)) / M_LN2);
        ShowOutput("PFFFT", n, complex, flops, t0, t1, max_iter);

        LOG(INFO) << "n = " << n << " SUCCESSFULLY";
      }
    }
  }

  return absl::OkStatus();
}

int main(int argc, char* argv[]) {
  // Initialize Google's logging library.
  google::InitGoogleLogging(argv[0]);

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (absl::Status status = PffftMain(); !status.ok()) {
    LOG(ERROR) << "Initialization failed: " << status.ToString();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
