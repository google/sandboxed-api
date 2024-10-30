#include "sandboxed_api/sandbox2/bpf_evaluator.h"

#include <linux/filter.h>

#include <cstdint>
#include <tuple>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "sandboxed_api/sandbox2/util/bpf_helper.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2::bpf {
namespace {

using ::testing::Eq;

TEST(EvaluatorTest, SimpleReturn) {
  sock_filter prog[] = {
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
  };
  SAPI_ASSERT_OK_AND_ASSIGN(uint32_t result, Evaluate(prog, {.nr = 1}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_ALLOW));
}

TEST(EvaluatorTest, ReturnAcumulator) {
  sock_filter prog[] = {
      BPF_STMT(BPF_LD + BPF_IMM, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET + BPF_A, 0),
  };
  SAPI_ASSERT_OK_AND_ASSIGN(uint32_t result, Evaluate(prog, {.nr = 1}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_ALLOW));
}

TEST(EvaluatorTest, SimpleJump) {
  sock_filter prog[] = {
      LOAD_SYSCALL_NR,
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 1, 0, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL),
  };
  SAPI_ASSERT_OK_AND_ASSIGN(uint32_t result, Evaluate(prog, {.nr = 1}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_ALLOW));
  SAPI_ASSERT_OK_AND_ASSIGN(result, Evaluate(prog, {.nr = 2}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_KILL));
}

TEST(EvaluatorTest, AbsoluteJump) {
  sock_filter prog[] = {
      BPF_STMT(BPF_JMP + BPF_JA, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL),
  };
  SAPI_ASSERT_OK_AND_ASSIGN(uint32_t result, Evaluate(prog, {.nr = 1}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_KILL));
}

TEST(EvaluatorTest, MemoryOps) {
  sock_filter prog[] = {
      BPF_STMT(BPF_LD + BPF_IMM, 0),
      BPF_STMT(BPF_LDX + BPF_IMM, 1),
      BPF_STMT(BPF_STX, 5),
      BPF_STMT(BPF_LD + BPF_MEM, 5),
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 1, 0, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL),
  };
  SAPI_ASSERT_OK_AND_ASSIGN(uint32_t result, Evaluate(prog, {}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_ALLOW));
}

TEST(EvaluatorTest, MemoryOps2) {
  sock_filter prog[] = {
      BPF_STMT(BPF_LDX + BPF_IMM, 1),
      BPF_STMT(BPF_LD + BPF_IMM, 0),
      BPF_STMT(BPF_ST, 5),
      BPF_STMT(BPF_LDX + BPF_MEM, 5),
      BPF_STMT(BPF_LD + BPF_IMM, 1),
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_X, 0, 0, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL),
  };
  SAPI_ASSERT_OK_AND_ASSIGN(uint32_t result, Evaluate(prog, {}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_KILL));
}

TEST(EvaluatorTest, Txa) {
  sock_filter prog[] = {
      BPF_STMT(BPF_LDX + BPF_IMM, 1),
      BPF_STMT(BPF_LD + BPF_IMM, 0),
      BPF_STMT(BPF_MISC + BPF_TXA, 0),
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 1, 0, 2),
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_X, 0, 0, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL),
  };
  SAPI_ASSERT_OK_AND_ASSIGN(uint32_t result, Evaluate(prog, {}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_ALLOW));
}

TEST(EvaluatorTest, Tax) {
  sock_filter prog[] = {
      BPF_STMT(BPF_LDX + BPF_IMM, 1),
      BPF_STMT(BPF_LD + BPF_IMM, 0),
      BPF_STMT(BPF_MISC + BPF_TAX, 0),
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 0, 0, 2),
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_X, 0, 0, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL),
  };
  SAPI_ASSERT_OK_AND_ASSIGN(uint32_t result, Evaluate(prog, {}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_ALLOW));
}

TEST(EvaluatorTest, LoadLen) {
  sock_filter prog[] = {
      BPF_STMT(BPF_LD + BPF_LEN, 0),
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, sizeof(struct seccomp_data), 0, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL),
  };
  SAPI_ASSERT_OK_AND_ASSIGN(uint32_t result, Evaluate(prog, {}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_ALLOW));
}

TEST(EvaluatorTest, LoadLenX) {
  sock_filter prog[] = {
      BPF_STMT(BPF_LDX + BPF_LEN, 0),
      BPF_STMT(BPF_LD + BPF_IMM, sizeof(struct seccomp_data)),
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_X, 0, 0, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL),
  };
  SAPI_ASSERT_OK_AND_ASSIGN(uint32_t result, Evaluate(prog, {}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_ALLOW));
}

TEST(EvaluatorTest, AllJumps) {
  std::vector<std::tuple<sock_filter, uint32_t, uint32_t>> jumps = {
      {BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 1, 0, 1), 1, 2},
      {BPF_JUMP(BPF_JMP + BPF_JGT + BPF_K, 1, 0, 1), 2, 1},
      {BPF_JUMP(BPF_JMP + BPF_JGE + BPF_K, 1, 0, 1), 1, 0},
      {BPF_JUMP(BPF_JMP + BPF_JSET + BPF_K, 3, 0, 1), 2, 12},
  };
  for (const auto& [jmp, allow_nr, kill_nr] : jumps) {
    std::vector<sock_filter> prog = {
        LOAD_SYSCALL_NR,
    };
    prog.push_back(jmp);
    prog.push_back(BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW));
    prog.push_back(BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL));
    SAPI_ASSERT_OK_AND_ASSIGN(uint32_t result,
                              Evaluate(prog, {.nr = allow_nr}));
    EXPECT_THAT(result, Eq(SECCOMP_RET_ALLOW));
    SAPI_ASSERT_OK_AND_ASSIGN(result, Evaluate(prog, {.nr = kill_nr}));
    EXPECT_THAT(result, Eq(SECCOMP_RET_KILL));
  }
}

TEST(EvaluatorTest, Arithmetics) {
  sock_filter prog[] = {
      LOAD_SYSCALL_NR,
      BPF_STMT(BPF_ALU + BPF_NEG, 1),
      BPF_STMT(BPF_ALU + BPF_ADD + BPF_K, 11),
      BPF_STMT(BPF_ALU + BPF_SUB + BPF_K, 5),
      BPF_STMT(BPF_ALU + BPF_MUL + BPF_K, 2),
      BPF_STMT(BPF_ALU + BPF_DIV + BPF_K, 10),
      BPF_STMT(BPF_ALU + BPF_OR + BPF_K, 2),
      BPF_STMT(BPF_ALU + BPF_AND + BPF_K, 1),
      BPF_STMT(BPF_ALU + BPF_LSH + BPF_K, 4),
      BPF_STMT(BPF_ALU + BPF_RSH + BPF_K, 1),
      BPF_STMT(BPF_ALU + BPF_XOR + BPF_K, 17),
      BPF_STMT(BPF_LDX + BPF_IMM, 2),
      BPF_STMT(BPF_ALU + BPF_ADD + BPF_X, 1),
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 27, 0, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_KILL),
  };
  SAPI_ASSERT_OK_AND_ASSIGN(uint32_t result, Evaluate(prog, {.nr = 1}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_ALLOW));
  SAPI_ASSERT_OK_AND_ASSIGN(result, Evaluate(prog, {.nr = 2}));
  EXPECT_THAT(result, Eq(SECCOMP_RET_KILL));
}

TEST(EvaluatorTest, InvalidDivision) {
  sock_filter prog[] = {
      BPF_STMT(BPF_LD + BPF_IMM, 1),
      BPF_STMT(BPF_ALU + BPF_DIV + BPF_K, 0),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
  };
  EXPECT_THAT(Evaluate(prog, {}),
              sapi::StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(EvaluatorTest, InvalidAluOp) {
  sock_filter prog[] = {
      BPF_STMT(BPF_LD + BPF_IMM, 1),
      BPF_STMT(BPF_ALU + 0xe0 + BPF_K, 10),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
  };
  EXPECT_THAT(Evaluate(prog, {}),
              sapi::StatusIs(absl::StatusCode::kInvalidArgument,
                             "Invalid instruction 228"));
}

TEST(EvaluatorTest, InvalidJump) {
  sock_filter prog[] = {
      BPF_STMT(BPF_LD + BPF_IMM, 1),
      BPF_JUMP(BPF_JMP + 0xe0 + BPF_K, 1, 0, 0),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
  };
  EXPECT_THAT(Evaluate(prog, {}),
              sapi::StatusIs(absl::StatusCode::kInvalidArgument,
                             "Invalid instruction 229"));
}

TEST(EvaluatorTest, InvalidInst) {
  sock_filter prog[] = {
      BPF_STMT(BPF_ST + BPF_X, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
  };
  EXPECT_THAT(Evaluate(prog, {}),
              sapi::StatusIs(absl::StatusCode::kInvalidArgument,
                             "Invalid instruction 10"));
}

TEST(EvaluatorTest, EmptyProgram) {
  EXPECT_THAT(Evaluate({}, {.nr = 1}),
              sapi::StatusIs(absl::StatusCode::kInvalidArgument,
                             "Out of bounds execution"));
}

TEST(EvaluatorTest, NoReturn) {
  sock_filter prog[] = {
      LOAD_SYSCALL_NR,
  };
  EXPECT_THAT(Evaluate(prog, {.nr = 1}),
              sapi::StatusIs(absl::StatusCode::kInvalidArgument,
                             "Fall through to out of bounds execution"));
}

TEST(EvaluatorTest, OutOfBoundsJump) {
  sock_filter prog[] = {
      LOAD_SYSCALL_NR,
      BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, 1, 0, 1),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
  };
  EXPECT_THAT(
      Evaluate(prog, {.nr = 2}),
      sapi::StatusIs(absl::StatusCode::kInvalidArgument, "Out of bounds jump"));
}

TEST(EvaluatorTest, OutOfMemoryOps) {
  std::vector<std::vector<sock_filter>> progs = {
      {
          BPF_STMT(BPF_LD + BPF_IMM, 1),
          BPF_STMT(BPF_ST, 17),
          BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      },
      {
          BPF_STMT(BPF_LDX + BPF_IMM, 1),
          BPF_STMT(BPF_STX, 17),
          BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      },
      {
          BPF_STMT(BPF_LD + BPF_MEM, 17),
          BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      },
      {
          BPF_STMT(BPF_LDX + BPF_MEM, 17),
          BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
      },
  };
  for (const std::vector<sock_filter>& prog : progs) {
    EXPECT_THAT(Evaluate(prog, {}),
                sapi::StatusIs(absl::StatusCode::kInvalidArgument));
  }
}

TEST(EvaluatorTest, MisalignedLoad) {
  sock_filter prog[] = {
      BPF_STMT(BPF_LD + BPF_W + BPF_ABS, 3),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
  };
  EXPECT_THAT(Evaluate(prog, {}),
              sapi::StatusIs(absl::StatusCode::kInvalidArgument,
                             "Misaligned read (3)"));
}

TEST(EvaluatorTest, OutOfBoundsLoad) {
  sock_filter prog[] = {
      BPF_STMT(BPF_LD + BPF_W + BPF_ABS, 4096),
      BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),
  };
  EXPECT_THAT(Evaluate(prog, {}),
              sapi::StatusIs(absl::StatusCode::kInvalidArgument,
                             "Out of bounds read (4096)"));
}

}  // namespace
}  // namespace sandbox2::bpf
