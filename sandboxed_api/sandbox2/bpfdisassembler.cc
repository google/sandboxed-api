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

#include "sandboxed_api/sandbox2/bpfdisassembler.h"

// IWYU pragma: no_include <asm/int-ll64.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#include <cstddef>

#include "absl/strings/str_cat.h"

#define INSIDE_FIELD(what, field)               \
  ((offsetof(seccomp_data, field) == 0 ||       \
    (what) >= offsetof(seccomp_data, field)) && \
   ((what) < (offsetof(seccomp_data, field) + sizeof(seccomp_data::field))))

namespace sandbox2 {
namespace bpf {
namespace {

std::string OperandToString(int op) {
  switch (op) {
    case BPF_ADD:
      return "+";
    case BPF_SUB:
      return "-";
    case BPF_MUL:
      return "*";
    case BPF_DIV:
      return "/";
    case BPF_XOR:
      return "^";
    case BPF_AND:
      return "&";
    case BPF_OR:
      return "|";
    case BPF_RSH:
      return ">>";
    case BPF_LSH:
      return "<<";
    default:
      return absl::StrCat("[unknown op ", op, "]");
  }
}

std::string ComparisonToString(int op) {
  switch (op) {
    case BPF_JGE:
      return ">=";
    case BPF_JGT:
      return ">";
    case BPF_JEQ:
      return "==";
    case BPF_JSET:
      return "&";
    default:
      return absl::StrCat("[unknown cmp ", op, "]");
  }
}

std::string NegatedComparisonToString(int op) {
  switch (op) {
    case BPF_JGE:
      return "<";
    case BPF_JGT:
      return "<=";
    case BPF_JEQ:
      return "!=";
    default:
      return absl::StrCat("[unknown neg cmp ", op, "]");
  }
}

}  // namespace

std::string DecodeInstruction(const sock_filter& inst, int pc) {
  constexpr auto kArgSize = sizeof(seccomp_data::args[0]);
  const int op = BPF_OP(inst.code);
  const int true_target = inst.jt + pc + 1;
  const int false_target = inst.jf + pc + 1;
  switch (inst.code) {
    case BPF_LD | BPF_W | BPF_ABS:
      if (inst.k & 3) {
        return absl::StrCat("A := *0x", absl::Hex(inst.k),
                            " (misaligned read)");
      }
      if (INSIDE_FIELD(inst.k, nr)) {
        return "A := syscall number";
      }
      if (INSIDE_FIELD(inst.k, arch)) {
        return "A := architecture";
      }
      if (INSIDE_FIELD(inst.k, instruction_pointer)) {
        // TODO(swiecki) handle big-endian.
        if (inst.k != offsetof(seccomp_data, instruction_pointer)) {
          return "A := instruction pointer high";
        }
        return "A := instruction pointer low";
      }
      if (INSIDE_FIELD(inst.k, args)) {
        const int argno = (inst.k - offsetof(seccomp_data, args)) / kArgSize;
        // TODO(swiecki) handle big-endian.
        if (inst.k != (offsetof(seccomp_data, args) + argno * kArgSize)) {
          return absl::StrCat("A := arg ", argno, " high");
        }
        return absl::StrCat("A := arg ", argno, " low");
      }
      return absl::StrCat("A := data[0x", absl::Hex(inst.k),
                          "] (invalid load)");
    case BPF_LD | BPF_W | BPF_LEN:
      return "A := sizeof(seccomp_data)";
    case BPF_LDX | BPF_W | BPF_LEN:
      return "X := sizeof(seccomp_data)";
    case BPF_LD | BPF_IMM:
      return absl::StrCat("A := 0x", absl::Hex(inst.k));
    case BPF_LDX | BPF_IMM:
      return absl::StrCat("X := 0x", absl::Hex(inst.k));
    case BPF_MISC | BPF_TAX:
      return "X := A";
    case BPF_MISC | BPF_TXA:
      return "A := X";
    case BPF_LD | BPF_MEM:
      return absl::StrCat("A := M[", inst.k, "]");
    case BPF_LDX | BPF_MEM:
      return absl::StrCat("X := M[", inst.k, "]");
    case BPF_ST:
      return absl::StrCat("M[", inst.k, "] := A");
    case BPF_STX:
      return absl::StrCat("M[", inst.k, "] := X");
    case BPF_RET | BPF_K: {
      __u32 data = inst.k & SECCOMP_RET_DATA;
#ifdef SECCOMP_RET_ACTION_FULL
      switch (inst.k & SECCOMP_RET_ACTION_FULL) {
#ifdef SECCOMP_RET_KILL_PROCESS
        case SECCOMP_RET_KILL_PROCESS:
          return "KILL_PROCESS";
#endif
#else
      switch (inst.k & SECCOMP_RET_ACTION) {
#endif
#ifdef SECCOMP_RET_LOG
        case SECCOMP_RET_LOG:
          return "LOG";
#endif
#ifdef SECCOMP_RET_USER_NOTIF
        case SECCOMP_RET_USER_NOTIF:
          return "USER_NOTIF";
#endif
        case SECCOMP_RET_KILL:
          return "KILL";
        case SECCOMP_RET_ALLOW:
          return "ALLOW";
        case SECCOMP_RET_TRAP:
          return absl::StrCat("TRAP 0x", absl::Hex(data));
        case SECCOMP_RET_ERRNO:
          return absl::StrCat("ERRNO 0x", absl::Hex(data));
        case SECCOMP_RET_TRACE:
          return absl::StrCat("TRACE 0x", absl::Hex(data));
        default:
          return absl::StrCat("return 0x", absl::Hex(inst.k));
      }
    }
    case BPF_RET | BPF_A:
      return "return A";
    case BPF_ALU | BPF_ADD | BPF_K:
    case BPF_ALU | BPF_SUB | BPF_K:
    case BPF_ALU | BPF_MUL | BPF_K:
    case BPF_ALU | BPF_DIV | BPF_K:
    case BPF_ALU | BPF_AND | BPF_K:
    case BPF_ALU | BPF_OR | BPF_K:
    case BPF_ALU | BPF_XOR | BPF_K:
    case BPF_ALU | BPF_LSH | BPF_K:
    case BPF_ALU | BPF_RSH | BPF_K:
      return absl::StrCat("A := A ", OperandToString(op), " 0x",
                          absl::Hex(inst.k));
    case BPF_ALU | BPF_ADD | BPF_X:
    case BPF_ALU | BPF_SUB | BPF_X:
    case BPF_ALU | BPF_MUL | BPF_X:
    case BPF_ALU | BPF_DIV | BPF_X:
    case BPF_ALU | BPF_AND | BPF_X:
    case BPF_ALU | BPF_OR | BPF_X:
    case BPF_ALU | BPF_XOR | BPF_X:
    case BPF_ALU | BPF_LSH | BPF_X:
    case BPF_ALU | BPF_RSH | BPF_X:
      return absl::StrCat("A := A ", OperandToString(op), " X");
    case BPF_ALU | BPF_NEG:
      return "A := -A";
    case BPF_JMP | BPF_JA:
      return absl::StrCat("jump to ", inst.k + pc + 1);
    case BPF_JMP | BPF_JEQ | BPF_K:
    case BPF_JMP | BPF_JGE | BPF_K:
    case BPF_JMP | BPF_JGT | BPF_K:
    case BPF_JMP | BPF_JSET | BPF_K:
      if (inst.jf == 0) {
        return absl::StrCat("if A ", ComparisonToString(op), " 0x",
                            absl::Hex(inst.k), " goto ", true_target);
      }
      if (inst.jt == 0 && op != BPF_JSET) {
        return absl::StrCat("if A ", NegatedComparisonToString(op), " 0x",
                            absl::Hex(inst.k), " goto ", false_target);
      }
      return absl::StrCat("if A ", ComparisonToString(op), " 0x",
                          absl::Hex(inst.k), " then ", true_target, " else ",
                          false_target);
    case BPF_JMP | BPF_JEQ | BPF_X:
    case BPF_JMP | BPF_JGE | BPF_X:
    case BPF_JMP | BPF_JGT | BPF_X:
    case BPF_JMP | BPF_JSET | BPF_X:
      if (inst.jf == 0) {
        return absl::StrCat("if A ", ComparisonToString(op), " X goto ",
                            true_target);
      }
      if (inst.jt == 0 && op != BPF_JSET) {
        return absl::StrCat("if A ", NegatedComparisonToString(op), " X goto ",
                            false_target);
      }
      return absl::StrCat("if A ", ComparisonToString(op), " X then ",
                          true_target, " else ", false_target);
    default:
      return absl::StrCat("Invalid instruction ", inst.code);
  }
}

std::string Disasm(absl::Span<const sock_filter> prog) {
  std::string rv;
  for (size_t i = 0; i < prog.size(); ++i) {
    absl::StrAppend(&rv, absl::Dec(i, absl::kZeroPad3), ": ",
                    DecodeInstruction(prog[i], i), "\n");
  }
  return rv;
}

}  // namespace bpf
}  // namespace sandbox2
