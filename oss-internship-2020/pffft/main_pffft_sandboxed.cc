#include <glog/logging.h>
#include <stdlib.h>
#include <string.h>
#include <sys/times.h>
#include <syscall.h>
#include <time.h>

#include <cassert>
#include <cmath>
#include <cstdio>

#include "fftpack.h"
#include "pffft_sapi.sapi.h"
#include "sandboxed_api/util/flag.h"
#include "sandboxed_api/vars.h"

ABSL_DECLARE_FLAG(string, sandbox2_danger_danger_permit_all);
ABSL_DECLARE_FLAG(string, sandbox2_danger_danger_permit_all_and_log);

class pffftSapiSandbox : public pffftSandbox {
 public:
  std::unique_ptr<sandbox2::Policy> ModifyPolicy(
      sandbox2::PolicyBuilder*) override {
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

double UclockSec(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }

int array_output_format = 0;

void ShowOutput(const char* name, int N, int cplx, float flops, float t0,
                float t1, int max_iter) {
  float mflops = flops / 1e6 / (t1 - t0 + 1e-16);
  if (array_output_format) {
    if (flops != -1) {
      printf("|%9.0f   ", mflops);
    } else
      printf("|      n/a   ");
  } else {
    if (flops != -1) {
      printf("N=%5d, %s %16s : %6.0f MFlops [t=%6.0f ns, %d runs]\n", N,
             (cplx ? "CPLX" : "REAL"), name, mflops,
             (t1 - t0) / 2 / max_iter * 1e9, max_iter);
    }
  }
  fflush(stdout);
}

int main(int argc, char* argv[]) {
  /*
   * Initialize Google's logging library.
   */
  google::InitGoogleLogging(argv[0]);

  gflags::ParseCommandLineFlags(&argc, &argv, true);
  /*
   * Nvalues is a vector keeping the values by which iterates N, its value
   * representing the input length. More concrete, N is the number of
   * data points the caclulus is up to (determinating its accuracy).
   * To show the performance of Fast-Fourier Transformations the program is
   * testing for various values of N.
   */
  int Nvalues[] = {64,    96,     128,        160,         192,     256,
                   384,   5 * 96, 512,        5 * 128,     3 * 256, 800,
                   1024,  2048,   2400,       4096,        8192,    9 * 1024,
                   16384, 32768,  256 * 1024, 1024 * 1024, -1};
  int i;

  LOG(INFO) << "Initializing sandbox...\n";

  pffftSapiSandbox sandbox;
  absl::Status init_status = sandbox.Init();

  LOG(INFO) << "Initialization: " << init_status.ToString().c_str() << "\n";

  pffftApi api(&sandbox);
  int cplx = 0;

  do {
    for (int N : Nvalues) {
      const int Nfloat = N * (cplx ? 2 : 1);
      int Nbytes = Nfloat * sizeof(float);

      float wrk[2 * Nfloat + 15 * sizeof(float)];
      sapi::v::Array<float> wrk_(wrk, 2 * Nfloat + 15 * sizeof(float));

      float X[Nbytes], Y[Nbytes], Z[Nbytes];
      sapi::v::Array<float> X_(X, Nbytes), Y_(Y, Nbytes), Z_(Z, Nbytes);

      double t0, t1, flops;

      int max_iter = 5120000 / N * 4;
#ifdef __arm__
      max_iter /= 4;
#endif
      int iter, k;

      for (k = 0; k < Nfloat; ++k) {
        X[k] = 0;
      }

      /*
       * FFTPack benchmark
       */
      {
        /*
         * SIMD_SZ == 4 (returning value of pffft_simd_size())
         */
        int max_iter_ = max_iter / 4;

        if (max_iter_ == 0) max_iter_ = 1;
        if (cplx) {
          api.cffti(N, wrk_.PtrBoth()).IgnoreError();
        } else {
          api.rffti(N, wrk_.PtrBoth()).IgnoreError();
        }
        t0 = UclockSec();

        for (iter = 0; iter < max_iter_; ++iter) {
          if (cplx) {
            api.cfftf(N, X_.PtrBoth(), wrk_.PtrBoth()).IgnoreError();
            api.cfftb(N, X_.PtrBoth(), wrk_.PtrBoth()).IgnoreError();
          } else {
            api.rfftf(N, X_.PtrBoth(), wrk_.PtrBoth()).IgnoreError();
            api.rfftb(N, X_.PtrBoth(), wrk_.PtrBoth()).IgnoreError();
          }
        }
        t1 = UclockSec();

        flops =
            (max_iter_ * 2) * ((cplx ? 5 : 2.5) * N * log((double)N) / M_LN2);
        ShowOutput("FFTPack", N, cplx, flops, t0, t1, max_iter_);
      }

      /*
       * PFFFT benchmark
       */
      {
        sapi::StatusOr<PFFFT_Setup*> s =
            api.pffft_new_setup(N, cplx ? PFFFT_COMPLEX : PFFFT_REAL);

        LOG(INFO) << "Setup status is: " << s.status().ToString().c_str()
                  << "\n";

        if (s.ok()) {
          sapi::v::RemotePtr s_reg(s.value());

          t0 = UclockSec();
          for (iter = 0; iter < max_iter; ++iter) {
            api.pffft_transform(&s_reg, X_.PtrBoth(), Z_.PtrBoth(),
                                Y_.PtrBoth(), PFFFT_FORWARD)
                .IgnoreError();
            api.pffft_transform(&s_reg, X_.PtrBoth(), Z_.PtrBoth(),
                                Y_.PtrBoth(), PFFFT_FORWARD)
                .IgnoreError();
          }

          t1 = UclockSec();
          api.pffft_destroy_setup(&s_reg).IgnoreError();

          flops =
              (max_iter * 2) * ((cplx ? 5 : 2.5) * N * log((double)N) / M_LN2);
          ShowOutput("PFFFT", N, cplx, flops, t0, t1, max_iter);
        }

        LOG(INFO) << "N = " << N << " SUCCESSFULLY\n\n";
      }
    }

    cplx = !cplx;
  } while (cplx);

  return 0;
}