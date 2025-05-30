#include "sandboxed_api/sandbox2/bpfdisassembler.h"

#include <linux/bpf_common.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"

namespace sandbox2::bpf {
namespace {

using ::testing::Eq;
using ::testing::StartsWith;

#ifndef SECCOMP_RET_USER_NOTIF
#define SECCOMP_RET_USER_NOTIF 0x7fc00000U /* notifies userspace */
#endif

#ifndef SECCOMP_RET_KILL_PROCESS
#define SECCOMP_RET_KILL_PROCESS 0x80000000U /* kill the process */
#endif

#ifndef SECCOMP_RET_LOG
#define SECCOMP_RET_LOG 0x7ffc0000U /* allow after logging */
#endif

TEST(DecodeInstructionTest, Loads) {
  EXPECT_THAT(DecodeInstruction(LOAD_ARCH, 1), Eq("A := architecture"));
  EXPECT_THAT(DecodeInstruction(LOAD_SYSCALL_NR, 1), Eq("A := syscall number"));
  EXPECT_THAT(DecodeInstruction(ARG_32(0), 1), Eq("A := arg 0 low"));
  EXPECT_THAT(
      DecodeInstruction(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, HI_ARG(0)), 1),
      Eq("A := arg 0 high"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_LD | BPF_W | BPF_LEN, 0), 1),
              Eq("A := sizeof(seccomp_data)"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_LDX | BPF_W | BPF_LEN, 0), 1),
              Eq("X := sizeof(seccomp_data)"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_LD | BPF_IMM, 0x1234), 1),
              Eq("A := 0x1234"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_LDX | BPF_IMM, 0x1234), 1),
              Eq("X := 0x1234"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_MISC | BPF_TAX, 0), 1),
              Eq("X := A"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_MISC | BPF_TXA, 0), 1),
              Eq("A := X"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0x1), 1),
              Eq("A := data[0x1] (misaligned load)"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, 0x1234), 1),
              Eq("A := data[0x1234] (invalid load)"));
}

TEST(DecodeInstructionTest, Memory) {
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_ST, 1), 1), Eq("M[1] := A"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_STX, 1), 1), Eq("M[1] := X"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_LD | BPF_MEM, 1), 1),
              Eq("A := M[1]"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_LDX | BPF_MEM, 1), 1),
              Eq("X := M[1]"));
}

TEST(DecodeInstructionTest, Returns) {
  EXPECT_THAT(DecodeInstruction(KILL, 1), Eq("KILL"));
  EXPECT_THAT(DecodeInstruction(ALLOW, 1), Eq("ALLOW"));
  EXPECT_THAT(DecodeInstruction(TRAP(0x12), 1), Eq("TRAP 0x12"));
  EXPECT_THAT(DecodeInstruction(ERRNO(0x23), 1), Eq("ERRNO 0x23"));
  EXPECT_THAT(DecodeInstruction(TRACE(0x34), 1), Eq("TRACE 0x34"));
  EXPECT_THAT(
      DecodeInstruction(BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_USER_NOTIF), 1),
      Eq("USER_NOTIF"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_LOG), 1),
              Eq("LOG"));
  EXPECT_THAT(
      DecodeInstruction(BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL_PROCESS), 1),
      Eq("KILL_PROCESS"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_RET + BPF_A, 0), 1),
              Eq("return A"));
}

TEST(DecodeInstructionTest, Alu) {
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_ALU | BPF_NEG, 0), 1),
              Eq("A := -A"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_ALU | BPF_ADD | BPF_K, 5), 1),
              Eq("A := A + 0x5"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_ALU | BPF_SUB | BPF_K, 5), 1),
              Eq("A := A - 0x5"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_ALU | BPF_DIV | BPF_X, 0), 1),
              Eq("A := A / X"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_ALU | BPF_MUL | BPF_X, 0), 1),
              Eq("A := A * X"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_ALU | BPF_AND | BPF_K, 6), 1),
              Eq("A := A & 0x6"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_ALU | BPF_OR | BPF_K, 7), 1),
              Eq("A := A | 0x7"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_ALU | BPF_XOR | BPF_K, 8), 1),
              Eq("A := A ^ 0x8"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_ALU | BPF_RSH | BPF_K, 9), 1),
              Eq("A := A >> 0x9"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_ALU | BPF_LSH | BPF_K, 1), 1),
              Eq("A := A << 0x1"));
}

TEST(DecodeInstructionTest, Jump) {
  EXPECT_THAT(
      DecodeInstruction(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x1234, 1, 0), 1),
      Eq("if A == 0x1234 goto 3"));
  EXPECT_THAT(
      DecodeInstruction(BPF_JUMP(BPF_JMP | BPF_JGT | BPF_K, 0x1234, 0, 1), 1),
      Eq("if A <= 0x1234 goto 3"));
  EXPECT_THAT(
      DecodeInstruction(BPF_JUMP(BPF_JMP | BPF_JGT | BPF_K, 0x1234, 1, 2), 1),
      Eq("if A > 0x1234 then 3 else 4"));
  EXPECT_THAT(
      DecodeInstruction(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_X, 1, 1, 0), 1),
      Eq("if A & X goto 3"));
  EXPECT_THAT(
      DecodeInstruction(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_X, 0, 0, 1), 1),
      Eq("if A < X goto 3"));
  EXPECT_THAT(
      DecodeInstruction(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_X, 0, 1, 2), 1),
      Eq("if A >= X then 3 else 4"));
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_JMP | BPF_K, 3), 1),
              Eq("jump to 5"));
}

TEST(DecodeInstructionTest, Invalid) {
  EXPECT_THAT(DecodeInstruction(BPF_STMT(BPF_LDX | BPF_W | BPF_ABS, 0), 1),
              StartsWith("Invalid instruction"));
}

TEST(DisasmTest, Simple) {
  EXPECT_THAT(Disasm({ALLOW}), Eq("000: ALLOW\n"));
  EXPECT_THAT(Disasm({KILL}), Eq("000: KILL\n"));
}

TEST(DisasmTest, Complex) {
  EXPECT_THAT(Disasm({LOAD_ARCH, JNE32(0x1, KILL), LOAD_SYSCALL_NR,
                      JEQ32(0x1234, ERRNO(0x33)), TRACE(0x22)}),
              Eq(R"(000: A := architecture
001: if A == 0x1 goto 3
002: KILL
003: A := syscall number
004: if A != 0x1234 goto 6
005: ERRNO 0x33
006: TRACE 0x22
)"));
}

}  // namespace
}  // namespace sandbox2::bpf
