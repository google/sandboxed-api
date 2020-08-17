#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/times.h>
#include <syscall.h>
#include <time.h>

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
        .DisableNamespaces()
        .BuildOrDie();
  }
};

double frand() { return rand() / (double)RAND_MAX; }

double uclock_sec(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }

int array_output_format = 0;

void show_output(const char* name, int N, int cplx, float flops, float t0,
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

/*
  For debug:
  SAPI_VLOG_LEVEL=1 ./pffft_sandboxed --v=100
  --sandbox2_danger_danger_permit_all_and_log my_aux_file
*/

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  int Nvalues[] = {64,    96,     128,        160,         192,     256,
                   384,   5 * 96, 512,        5 * 128,     3 * 256, 800,
                   1024,  2048,   2400,       4096,        8192,    9 * 1024,
                   16384, 32768,  256 * 1024, 1024 * 1024, -1};
  int i;

  printf("initializing sandbox...\n");

  pffftSapiSandbox sandbox;
  sandbox.Init().IgnoreError();

  pffftApi api(&sandbox);

  int N, cplx;

  cplx = 0;

  for (i = 0; i < 5; i++) {
    N = Nvalues[i];

    int Nfloat = N * (cplx ? 2 : 1);
    int Nbytes = Nfloat * sizeof(float);
    int pass;

    float ref[Nbytes], in[Nbytes], out[Nbytes], tmp[Nbytes], tmp2[Nbytes];

    sapi::v::Array<float> ref_(ref, Nbytes);
    sapi::v::Array<float> in_(in, Nbytes);
    sapi::v::Array<float> out_(out, Nbytes);
    sapi::v::Array<float> tmp_(tmp, Nbytes);
    sapi::v::Array<float> tmp2_(tmp2, Nbytes);

    float wrk[2 * Nbytes + 15 * sizeof(float)];
    sapi::v::Array<float> wrk_(wrk, 2 * Nbytes + 15 * sizeof(float));

    float ref_max = 0;
    int k;

    Nfloat = (cplx ? N * 2 : N);
    float X[Nbytes], Y[Nbytes], Z[Nbytes];
    sapi::v::Array<float> X_(X, Nbytes), Y_(Y, Nbytes), Z_(Z, Nbytes);

    double t0, t1, flops;

    int max_iter = 5120000 / N * 4;
#ifdef __arm__
    max_iter /= 4;
#endif
    int iter;

    for (k = 0; k < Nfloat; ++k) {
      X[k] = 0;
    }

    // FFTPack benchmark
    {
      int max_iter_ =
          max_iter / 4;  // SIMD_SZ == 4 (returning value of pffft_simd_size())
      if (max_iter_ == 0) max_iter_ = 1;
      if (cplx) {
        api.cffti(N, wrk_.PtrBoth()).IgnoreError();
      } else {
        api.rffti(N, wrk_.PtrBoth()).IgnoreError();
      }
      t0 = uclock_sec();

      for (iter = 0; iter < max_iter_; ++iter) {
        if (cplx) {
          api.cfftf(N, X_.PtrBoth(), wrk_.PtrBoth()).IgnoreError();
          api.cfftb(N, X_.PtrBoth(), wrk_.PtrBoth()).IgnoreError();
        } else {
          api.rfftf(N, X_.PtrBoth(), wrk_.PtrBoth()).IgnoreError();
          api.rfftb(N, X_.PtrBoth(), wrk_.PtrBoth()).IgnoreError();
        }
      }
      t1 = uclock_sec();

      flops = (max_iter_ * 2) * ((cplx ? 5 : 2.5) * N * log((double)N) / M_LN2);
      show_output("FFTPack", N, cplx, flops, t0, t1, max_iter_);
    }
  }

  return 0;
}