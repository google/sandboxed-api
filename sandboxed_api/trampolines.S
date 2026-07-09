// Copyright 2026 Google LLC
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

#if defined(__x86_64__)

.intel_syntax noprefix

.global sapi_callback_trampolines
.global sapi_callback_trampolines_end
.extern sapi_client_HandleCallback

.section .text

.align 16
sapi_callback_handler:
    // Save argument registers (x86_64 ABI)
    push rbp
    mov rbp, rsp
    push r9
    push r8
    push rcx
    push rdx
    push rsi
    push rdi

    // Call C++ handler:
    // uint64_t sapi_client_HandleCallback(uint64_t index, uint64_t* args);
    mov rdi, r11 // index
    mov rsi, rsp // pointer to saved registers
    call sapi_client_HandleCallback

    // Clean up stack
    add rsp, 48
    pop rbp
    ret

.align 16
sapi_callback_trampolines:
.macro trampoline
    mov r11, \@
    jmp sapi_callback_handler
    .align 16
.endm

// Generate 64 trampolines
.rept 64
    trampoline
.endr
sapi_callback_trampolines_end:

#elif defined(__aarch64__)

.global sapi_callback_trampolines
.global sapi_callback_trampolines_end
.extern sapi_client_HandleCallback

.section .text

.align 16
sapi_callback_handler:
    // Save FP, LR and make room for x0-x7 (80 bytes total)
    // Stack must be 16-byte aligned.
    stp x29, x30, [sp, #-80]!
    mov x29, sp

    // Save argument registers x0-x7 to stack
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]

    // Call C++ handler:
    // uint64_t sapi_client_HandleCallback(uint64_t index, uint64_t* args);
    mov x0, x9 // index
    add x1, sp, #16 // pointer to saved registers (x0-x7)
    bl sapi_client_HandleCallback

    // Restore FP, LR and pop stack
    ldp x29, x30, [sp], #80
    ret

.align 16
sapi_callback_trampolines:
.macro trampoline
    mov x9, \@
    b sapi_callback_handler
    .align 16
.endm

// Generate 64 trampolines
.rept 64
    trampoline
.endr
sapi_callback_trampolines_end:

#endif
