#include "sandboxed_api/sandbox2/bpf_evaluator.h"

#include <linux/bpf_common.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "sandboxed_api/util/status_macros.h"

namespace sandbox2::bpf {
namespace {

absl::StatusOr<uint32_t> EvaluateAlu(uint16_t op, uint32_t a, uint32_t b) {
  switch (op) {
    case BPF_ADD:
      return a + b;
    case BPF_SUB:
      return a - b;
    case BPF_MUL:
      return a * b;
    case BPF_DIV:
      if (b == 0) {
        return absl::InvalidArgumentError("Division by zero");
      }
      return a / b;
    case BPF_OR:
      return a | b;
    case BPF_AND:
      return a & b;
    case BPF_XOR:
      return a ^ b;
    case BPF_LSH:
      return a << b;
    case BPF_RSH:
      return a >> b;
    case BPF_NEG:
      return -a;
    default:
      return absl::InvalidArgumentError("Invalid ALU operation");
  }
}

absl::StatusOr<bool> EvaluateCmp(uint16_t cmp, uint32_t a, uint32_t b) {
  switch (cmp) {
    case BPF_JEQ:
      return a == b;
    case BPF_JGT:
      return a > b;
    case BPF_JGE:
      return a >= b;
    case BPF_JSET:
      return (a & b) != 0;
    default:
      return absl::InvalidArgumentError("Invalid jump operation");
  }
}

class Interpreter {
 public:
  Interpreter(absl::Span<const sock_filter> prog,
              const struct seccomp_data& data)
      : prog_(prog), data_(data) {}
  absl::StatusOr<uint32_t> Evaluate();

 private:
  absl::Status EvaluateSingleInstruction();

  absl::Span<const sock_filter> prog_;
  const struct seccomp_data& data_;
  uint32_t pc_;
  uint32_t accumulator_;
  uint32_t x_reg_;
  uint32_t mem_[16];
  std::optional<uint32_t> result_;
};

absl::Status Interpreter::EvaluateSingleInstruction() {
  if (pc_ >= prog_.size()) {
    return absl::InvalidArgumentError("Out of bounds execution");
  }
  const sock_filter& inst = prog_[pc_];
  uint32_t offset = 0;
  switch (inst.code) {
    case BPF_LD | BPF_W | BPF_ABS: {
      constexpr uint32_t kAlignmentMask = 3;
      if (inst.k & kAlignmentMask) {
        return absl::InvalidArgumentError(
            absl::StrCat("Misaligned read (", inst.k, ")"));
      }
      if (inst.k + sizeof(accumulator_) > sizeof(data_)) {
        return absl::InvalidArgumentError(
            absl::StrCat("Out of bounds read (", inst.k, ")"));
      }
      memcpy(&accumulator_, &(reinterpret_cast<const char*>(&data_)[inst.k]),
             sizeof(accumulator_));
      break;
    }
    case BPF_LD | BPF_W | BPF_LEN:
      accumulator_ = sizeof(struct seccomp_data);
      break;
    case BPF_LDX | BPF_W | BPF_LEN:
      x_reg_ = sizeof(struct seccomp_data);
      break;
    case BPF_LD | BPF_IMM:
      accumulator_ = inst.k;
      break;
    case BPF_LDX | BPF_IMM:
      x_reg_ = inst.k;
      break;
    case BPF_MISC | BPF_TAX:
      x_reg_ = accumulator_;
      break;
    case BPF_MISC | BPF_TXA:
      accumulator_ = x_reg_;
      break;
    case BPF_LD | BPF_MEM:
      if (inst.k >= sizeof(mem_) / sizeof(mem_[0])) {
        return absl::InvalidArgumentError(
            absl::StrCat("Out of bounds memory load (", inst.k, " >= 16)"));
      }
      accumulator_ = mem_[inst.k];
      break;
    case BPF_LDX | BPF_MEM:
      if (inst.k >= sizeof(mem_) / sizeof(mem_[0])) {
        return absl::InvalidArgumentError(
            absl::StrCat("Out of bounds memory load (", inst.k, " >= 16)"));
      }
      x_reg_ = mem_[inst.k];
      break;
    case BPF_ST:
      if (inst.k >= sizeof(mem_) / sizeof(mem_[0])) {
        return absl::InvalidArgumentError(
            absl::StrCat("Out of bounds memory store (", inst.k, " >= 16)"));
      }
      mem_[inst.k] = accumulator_;
      break;
    case BPF_STX:
      if (inst.k >= sizeof(mem_) / sizeof(mem_[0])) {
        return absl::InvalidArgumentError(
            absl::StrCat("Out of bounds memory store (", inst.k, " >= 16)"));
      }
      mem_[inst.k] = x_reg_;
      break;
    case BPF_RET | BPF_K:
      result_ = inst.k;
      return absl::OkStatus();
    case BPF_RET | BPF_A:
      result_ = accumulator_;
      return absl::OkStatus();
    case BPF_ALU | BPF_ADD | BPF_K:
    case BPF_ALU | BPF_SUB | BPF_K:
    case BPF_ALU | BPF_MUL | BPF_K:
    case BPF_ALU | BPF_DIV | BPF_K:
    case BPF_ALU | BPF_AND | BPF_K:
    case BPF_ALU | BPF_OR | BPF_K:
    case BPF_ALU | BPF_XOR | BPF_K:
    case BPF_ALU | BPF_LSH | BPF_K:
    case BPF_ALU | BPF_RSH | BPF_K:
    case BPF_ALU | BPF_ADD | BPF_X:
    case BPF_ALU | BPF_SUB | BPF_X:
    case BPF_ALU | BPF_MUL | BPF_X:
    case BPF_ALU | BPF_DIV | BPF_X:
    case BPF_ALU | BPF_AND | BPF_X:
    case BPF_ALU | BPF_OR | BPF_X:
    case BPF_ALU | BPF_XOR | BPF_X:
    case BPF_ALU | BPF_LSH | BPF_X:
    case BPF_ALU | BPF_RSH | BPF_X:
    case BPF_ALU | BPF_NEG: {
      uint32_t val = BPF_SRC(inst.code) == BPF_K ? inst.k : x_reg_;
      SAPI_ASSIGN_OR_RETURN(accumulator_,
                            EvaluateAlu(BPF_OP(inst.code), accumulator_, val));
      break;
    }
    case BPF_JMP | BPF_JA:
      offset = inst.k;
      break;
    case BPF_JMP | BPF_JEQ | BPF_K:
    case BPF_JMP | BPF_JGE | BPF_K:
    case BPF_JMP | BPF_JGT | BPF_K:
    case BPF_JMP | BPF_JSET | BPF_K:
    case BPF_JMP | BPF_JEQ | BPF_X:
    case BPF_JMP | BPF_JGE | BPF_X:
    case BPF_JMP | BPF_JGT | BPF_X:
    case BPF_JMP | BPF_JSET | BPF_X: {
      uint32_t val = BPF_SRC(inst.code) == BPF_K ? inst.k : x_reg_;
      SAPI_ASSIGN_OR_RETURN(bool cond,
                            EvaluateCmp(BPF_OP(inst.code), accumulator_, val));
      offset = cond ? inst.jt : inst.jf;
      break;
    }
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid instruction ", inst.code));
  }
  if (pc_ > std::numeric_limits<uint32_t>::max() - 1 ||
      pc_ + 1 >= prog_.size()) {
    return absl::InvalidArgumentError(
        "Fall through to out of bounds execution");
  }
  pc_ += 1;
  if (offset != 0 && (pc_ > std::numeric_limits<uint32_t>::max() - offset ||
                      pc_ + offset >= prog_.size())) {
    return absl::InvalidArgumentError("Out of bounds jump");
  }
  pc_ += offset;
  return absl::OkStatus();
}

absl::StatusOr<uint32_t> Interpreter::Evaluate() {
  pc_ = 0;
  result_ = std::nullopt;
  while (!result_.has_value()) {
    SAPI_RETURN_IF_ERROR(EvaluateSingleInstruction());
  }
  return *result_;
}

}  // namespace

absl::StatusOr<uint32_t> Evaluate(absl::Span<const sock_filter> prog,
                                  const struct seccomp_data& data) {
  Interpreter interpreter(prog, data);
  return interpreter.Evaluate();
}

}  // namespace sandbox2::bpf
