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
                         pffft - not implemented
