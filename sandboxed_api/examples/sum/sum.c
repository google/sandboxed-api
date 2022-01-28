// Copyright 2019 Google LLC
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

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ptrace.h>

int sumsymbol = 5;

typedef struct sum_params_s {
  int a;
  int b;
  int ret;
} sum_params;

extern int ftest(FILE *f) {
  return f->_offset;
}

extern int sum(int a, int b) {
  return a + b;
}

extern void sums(sum_params* params) {
  params->ret =  params->a + params->b;
}

extern long double addf(float a, double b, long double c) {
  return a + b + c;
}

extern int sub(int a, int b) {
  return a - b;
}

extern int mul(int a, int b) {
  return a * b;
}

extern int divs(int a, int b) {
  return a / b;
}

extern double muld(double a, float b) {
  return a * b;
}

extern void crash(void) {
  void(*die)() = (void(*)())(0x0000dead);
  die();
}

extern void violate(void) {
  // Issue a PTRACE_CONT that will always fail, since we are not in stopped
  // state. The actual call should be caught by the sandbox policy.
  ptrace(PTRACE_CONT, 0, NULL, NULL);
}

extern int sumarr(int* input, size_t nelem) {
  int s = 0, i;
  for (i = 0; i < nelem; i++) {
    s += input[i];
  }
  return s;
}

extern void testptr(void *ptr) {
  if (ptr) {
    puts("Is Not a NULL-ptr");
  } else {
    puts("Is a NULL-ptr");
  }
}

extern int read_int(int fd) {
  char buf[10] = {0};
  int ret = read(fd, buf, sizeof(buf) - 1);
  if(ret > 0) {
    ret = atoi(buf);
  }
  return ret;
}

extern void sleep_for_sec(int sec) {
  sleep(sec);
}
