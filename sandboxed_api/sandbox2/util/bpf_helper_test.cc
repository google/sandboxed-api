#include "sandboxed_api/sandbox2/util/bpf_helper.h"

#include <linux/filter.h>

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"

namespace {

void AddMaxLabels(bpf_labels& labels) {
  static std::vector<std::string>* label_strs = []() {
    auto* label_strs = new std::vector<std::string>();
    for (int i = 0; i < BPF_LABELS_MAX; ++i) {
      label_strs->push_back(absl::StrCat("lbl", i));
    }
    return label_strs;
  }();
  for (int i = 0; i < BPF_LABELS_MAX; ++i) {
    seccomp_bpf_label(&labels, (*label_strs)[i].c_str());
  }
}

TEST(BpfHelperTest, MaxLabels) {
  bpf_labels labels = {};
  AddMaxLabels(labels);
  std::vector<struct sock_filter> filter = {
      ALLOW,
  };
  EXPECT_EQ(bpf_resolve_jumps(&labels, filter.data(), filter.size()), 0);
}

TEST(BpfHelperTest, LabelOverflow) {
  bpf_labels labels = {};
  AddMaxLabels(labels);
  std::vector<struct sock_filter> filter = {
      JUMP(&labels, overflow),
      LABEL(&labels, overflow),
      ALLOW,
  };
  filter.push_back(ALLOW);
  EXPECT_NE(bpf_resolve_jumps(&labels, filter.data(), filter.size()), 0);
}

TEST(BpfHelperTest, UnresolvedLabel) {
  bpf_labels labels = {};
  std::vector<struct sock_filter> filter = {
      JUMP(&labels, unresolved),
      LABEL(&labels, unused),
  };
  filter.push_back(ALLOW);
  EXPECT_NE(bpf_resolve_jumps(&labels, filter.data(), filter.size()), 0);
}

TEST(BpfHelperTest, BackwardJump) {
  bpf_labels labels = {};
  std::vector<struct sock_filter> filter = {
      LABEL(&labels, backward),
      JUMP(&labels, backward),
  };
  filter.push_back(ALLOW);
  EXPECT_NE(bpf_resolve_jumps(&labels, filter.data(), filter.size()), 0);
}

TEST(BpfHelperTest, Duplicate) {
  bpf_labels labels = {};
  std::vector<struct sock_filter> filter = {
      JUMP(&labels, dup),
      LABEL(&labels, dup),
      LABEL(&labels, dup),
  };
  filter.push_back(ALLOW);
  EXPECT_NE(bpf_resolve_jumps(&labels, filter.data(), filter.size()), 0);
}

TEST(BpfHelperTest, OutOfBoundsLabel) {
  bpf_labels labels = {};
  std::vector<struct sock_filter> filter = {
      JUMP(&labels, normal),
      LABEL(&labels, normal),
      BPF_JUMP(BPF_JMP + BPF_JA, 1, JUMP_JT, JUMP_JF),
  };
  filter.push_back(ALLOW);
  EXPECT_NE(bpf_resolve_jumps(&labels, filter.data(), filter.size()), 0);
}

}  // namespace
