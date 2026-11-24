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

// Trampoline layout shared with Sandbox2RPCChannel:
//   5 groups (stack_arg_count = 0..4) * 64 slots = 320 trampolines, each
//   16 bytes. Entry `id` in [0, 320) encodes `stack_arg_count = id >> 6` and
//   `slot = id & 63`, so `sapi_callback_handler` reads only the stack argument
//   slots actually passed by the caller (avoiding faults on guard pages just
//   above minimal stacks or MTE tag mismatches in caller stack frames).

#if defined(__x86_64__)

.intel_syntax noprefix

.global sapi_callback_trampolines
.global sapi_callback_trampolines_end
.extern sapi_client_HandleCallback

.section .text

.balign 16
sapi_callback_handler:
    // Save argument registers and caller-passed stack slots (x86_64 SysV ABI):
    //   [rsp + 0..47]:    rdi, rsi, rdx, rcx, r8, r9 (6 int regs, slots 0..5)
    //   [rsp + 48..63]:   reserved int slots 6..7 (zeroed)
    //   [rsp + 64..127]:  xmm0..xmm7 (8 float/double regs, slots 8..15)
    //   [rsp + 128..159]: caller stack arguments [rbp+16..40] (slots 16..19;
    //                     zeroed first, then only `r11 >> 6` slots are loaded)
    push rbp
    mov rbp, rsp
    sub rsp, 160

    mov [rsp + 0], rdi
    mov [rsp + 8], rsi
    mov [rsp + 16], rdx
    mov [rsp + 24], rcx
    mov [rsp + 32], r8
    mov [rsp + 40], r9
    xor eax, eax
    mov [rsp + 48], rax
    mov [rsp + 56], rax

    movq [rsp + 64], xmm0
    movq [rsp + 72], xmm1
    movq [rsp + 80], xmm2
    movq [rsp + 88], xmm3
    movq [rsp + 96], xmm4
    movq [rsp + 104], xmm5
    movq [rsp + 112], xmm6
    movq [rsp + 120], xmm7

    mov [rsp + 128], rax
    mov [rsp + 136], rax
    mov [rsp + 144], rax
    mov [rsp + 152], rax

    mov rdi, r11
    and edi, 63
    shr r11, 6
    jz .Ldone_stack_args

    mov rax, [rbp + 16]
    mov [rsp + 128], rax
    cmp r11, 1
    je .Ldone_stack_args

    mov rax, [rbp + 24]
    mov [rsp + 136], rax
    cmp r11, 2
    je .Ldone_stack_args

    mov rax, [rbp + 32]
    mov [rsp + 144], rax
    cmp r11, 3
    je .Ldone_stack_args

    mov rax, [rbp + 40]
    mov [rsp + 152], rax

.Ldone_stack_args:
    // Call C++ handler:
    // uint64_t sapi_client_HandleCallback(uint64_t index, uint64_t* args);
    // `rdi` holds `index` (r11 & 63).
    mov rsi, rsp // pointer to saved registers
    call sapi_client_HandleCallback

    // Populate xmm0 in case the callback returns float or double.
    movq xmm0, rax

    // Clean up stack
    add rsp, 160
    pop rbp
    ret

.balign 16
sapi_callback_trampolines:
.macro trampoline
    mov r11, \@
    jmp sapi_callback_handler
    .balign 16
.endm

// Generate 5 * 64 = 320 trampolines (stack_arg_count 0..4, slots 0..63).
.rept 320
    trampoline
.endr
sapi_callback_trampolines_end:

#elif defined(__aarch64__)

.global sapi_callback_trampolines
.global sapi_callback_trampolines_end
.extern sapi_client_HandleCallback

.section .text

.balign 16
sapi_callback_handler:
    // Save FP, LR and make room for 20 uint64_t slots (176 bytes total).
    // Stack must be 16-byte aligned.
    //   [sp + 16..79]:   x0..x7 (8 int regs, slots 0..7)
    //   [sp + 80..143]:  d0..d7 (8 float/double regs, slots 8..15)
    //   [sp + 144..175]: caller stack arguments [x29+176..200] (slots 16..19;
    //                    zeroed first, then only `x9 >> 6` 8-byte slots loaded)
    stp x29, x30, [sp, #-176]!
    mov x29, sp

    // Save argument registers x0-x7 to stack
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]

    // Save floating-point argument registers d0-d7 to stack
    stp d0, d1, [sp, #80]
    stp d2, d3, [sp, #96]
    stp d4, d5, [sp, #112]
    stp d6, d7, [sp, #128]

    stp xzr, xzr, [sp, #144]
    stp xzr, xzr, [sp, #160]

    and x0, x9, #63
    lsr x9, x9, #6
    cbz x9, .Ldone_stack_args_aarch64

    ldr x10, [x29, #176]
    str x10, [sp, #144]
    cmp x9, #1
    b.eq .Ldone_stack_args_aarch64

    ldr x10, [x29, #184]
    str x10, [sp, #152]
    cmp x9, #2
    b.eq .Ldone_stack_args_aarch64

    ldr x10, [x29, #192]
    str x10, [sp, #160]
    cmp x9, #3
    b.eq .Ldone_stack_args_aarch64

    ldr x10, [x29, #200]
    str x10, [sp, #168]

.Ldone_stack_args_aarch64:
    // Call C++ handler:
    // uint64_t sapi_client_HandleCallback(uint64_t index, uint64_t* args);
    // `x0` holds `index` (x9 & 63).
    add x1, sp, #16 // pointer to saved registers
    bl sapi_client_HandleCallback

    // Populate d0 in case the callback returns float or double.
    fmov d0, x0

    // Restore FP, LR and pop stack
    ldp x29, x30, [sp], #176
    ret

.balign 16
sapi_callback_trampolines:
.macro trampoline
    mov x9, \@
    b sapi_callback_handler
    .balign 16
.endm

// Generate 5 * 64 = 320 trampolines (stack_arg_count 0..4, slots 0..63).
.rept 320
    trampoline
.endr
sapi_callback_trampolines_end:

#elif defined(__riscv)

.global sapi_callback_trampolines
.global sapi_callback_trampolines_end
.extern sapi_client_HandleCallback

.section .text

// Each trampoline occupies exactly 16 bytes (see kTrampolineSize in
// sandbox2_rpcchannel.h). .balign alone would guarantee that, but disabling
// compressed instructions and linker relaxation keeps the layout fixed at
// assembly time and easy to reason about.
.option push
.option norvc
.option norelax

.balign 16
sapi_callback_handler:
    // Make room for 20 uint64_t slots, s0 (FP) and ra (176 bytes total).
    // Stack must be 16-byte aligned.
    //   0..56(sp):    a0..a7 (8 int regs, slots 0..7)
    //   64..120(sp):  fa0..fa7 (8 float/double regs, slots 8..15)
    //   128..152(sp): caller stack arguments 0..24(s0) (slots 16..19;
    //                 zeroed first, then only `t0 >> 6` slots are loaded)
    //   160(sp):      saved s0
    //   168(sp):      saved ra
    addi sp, sp, -176
    sd ra, 168(sp)
    sd s0, 160(sp)
    addi s0, sp, 176

    // Save argument registers a0-a7 to stack
    sd a0, 0(sp)
    sd a1, 8(sp)
    sd a2, 16(sp)
    sd a3, 24(sp)
    sd a4, 32(sp)
    sd a5, 40(sp)
    sd a6, 48(sp)
    sd a7, 56(sp)

#if defined(__riscv_float_abi_double)
    // Save floating-point argument registers fa0-fa7 to stack
    fsd fa0, 64(sp)
    fsd fa1, 72(sp)
    fsd fa2, 80(sp)
    fsd fa3, 88(sp)
    fsd fa4, 96(sp)
    fsd fa5, 104(sp)
    fsd fa6, 112(sp)
    fsd fa7, 120(sp)
#else
    sd zero, 64(sp)
    sd zero, 72(sp)
    sd zero, 80(sp)
    sd zero, 88(sp)
    sd zero, 96(sp)
    sd zero, 104(sp)
    sd zero, 112(sp)
    sd zero, 120(sp)
#endif

    sd zero, 128(sp)
    sd zero, 136(sp)
    sd zero, 144(sp)
    sd zero, 152(sp)

    andi a0, t0, 63
    srli t0, t0, 6
    beqz t0, .Ldone_stack_args_riscv

    ld t1, 0(s0)
    sd t1, 128(sp)
    li t2, 1
    beq t0, t2, .Ldone_stack_args_riscv

    ld t1, 8(s0)
    sd t1, 136(sp)
    li t2, 2
    beq t0, t2, .Ldone_stack_args_riscv

    ld t1, 16(s0)
    sd t1, 144(sp)
    li t2, 3
    beq t0, t2, .Ldone_stack_args_riscv

    ld t1, 24(s0)
    sd t1, 152(sp)

.Ldone_stack_args_riscv:
    // Call C++ handler:
    // uint64_t sapi_client_HandleCallback(uint64_t index, uint64_t* args);
    // `a0` holds `index` (t0 & 63).
    mv a1, sp // pointer to saved registers
    call sapi_client_HandleCallback

#if defined(__riscv_float_abi_double)
    // Populate fa0 in case the callback returns float or double.
    fmv.d.x fa0, a0
#endif

    // Restore FP, RA and pop stack
    ld ra, 168(sp)
    ld s0, 160(sp)
    addi sp, sp, 176
    ret

.balign 16
sapi_callback_trampolines:
.macro trampoline
    li t0, \@
    j sapi_callback_handler
    .balign 16
.endm

// Generate 5 * 64 = 320 trampolines (stack_arg_count 0..4, slots 0..63).
.rept 320
    trampoline
.endr
sapi_callback_trampolines_end:

.option pop

#endif
