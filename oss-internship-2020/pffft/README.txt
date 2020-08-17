Sandboxing PFFFT library

Builder: CMake

For testing: 
`cd build`, then `./pffft_sandboxed`

For debug:
`SAPI_VLOG_LEVEL=1 ./pffft_sandboxed --v=100
--sandbox2_danger_danger_permit_all_and_log <auxiliar file>`

CMake observations:
    * linking pffft and fftpack (which contains necessary functions for pffft)
    * set math library 

Sandboxed main observations:
    * containing two testing parts (fft / pffft benchmarks)
        ! current stage: fft - works :)
                         pffft - implemented
    * pffft benchmark bug: "Sandbox not active" 
                            => loop in pffft_transform for N = 64 (why?); 
                               N = 64, status OK, pffft_transform generates error 
                               N > 64, status not OK
                               Problem on initialising sapi::StatusOr<PFFFT_Setup *> s; 
                               the memory that stays for s is not the same with the address passed
                               in pffft_transform function. 
                               (sapi::v::GenericPtr to be changed?)
                               
                               Temporary solution (not done): change the generated files to accept
                               uintptr_t instead of PFFFT_Setup
