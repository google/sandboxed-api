# Sandboxing PFFFT library

Builder: CMake  
OS: Linux

### For testing: 
`cd build`, then `./pffft_sandboxed`

### For debug:
`SAPI_VLOG_LEVEL=1 ./pffft_sandboxed --v=100
--sandbox2_danger_danger_permit_all_and_log <auxiliar file>`

## ***About the project*** 
*PFFFT library is concerned with 1D Fast-Fourier Transformations finding a
compromise between accuracy and speed. It deals with real and complex
vectors, both cases being illustrated in the testing part (`main_pffft.c` 
for initially and original version, `main_pffft_sandboxed.cc` for our 
currently implemented sandboxed version).
The original files can be found at: https://bitbucket.org/jpommier/pffft/src.*

*The purpose of sandboxing is to limit the permissions and capabilities of 
library’s methods, in order to secure the usage of them. 
After obtaining the sandbox, the functions will be called through an 
Sandbox API (being called `api` in the current test) and so, the 
operations, system calls or namspaces access may be controlled. 
From both `pffft.h` and `fftpack.h` headers, useful methods are added to 
sapi library builded with CMake. There is also a need to link math library 
as the transformations made require mathematical operators. 
Regarding the testing of the methods, one main is doing this job by 
iterating through a set of values, that represents the accuracy of 
transformations and print the speed for each value and type of 
transformation. More specifically, the input length is the target for 
accuracy (named as `N`) and it stands for the number of data points from 
the series that calculate the result of transformation. It is also 
important to mention that the `cplx` variable stands for a boolean value 
that tells the type of transformation (0 for REAL and 1 for COMPLEX) and 
it is taken into account while testing.
In the end, the performance of PFFFT library it is outlined by the output.*

#### CMake observations resume:
    * linking pffft and fftpack (which contains necessary functions for pffft)
    * set math library 

#### Sandboxed main observations resume:
    * containing two testing parts (fft / pffft benchmarks)
    * showing the performance of the transformations implies 
    testing them through various FFT dimenstions. 
    Variable N, the input length, will take specific values 
    meaning the number of points to which it is set the calculus 
    (more details of mathematical purpose of N - https://en.wikipedia.org/wiki/Cooley%E2%80%93Tukey_FFT_algorithm). 
    * output shows speed depending on the input length

    

### Bugs history
    - [Solved] pffft benchmark bug: "Sandbox not active"  
    N = 64, status OK, pffft_transform generates error 
    N > 64, status not OK
    Problem on initialising sapi::StatusOr<PFFFT_Setup *> s; the memory that stays 
    for s is not the same with the address passed in pffft_transform function. 
    (sapi :: v :: GenericPtr - to be changed)

    Temporary solution: change the generated files to accept 
    uintptr_t instead of PFFFT_Setup

    Solution: using "sapi::v::RemotePtr" instead of "sapi::v::GenericPtr" 
    to access the memory of object s