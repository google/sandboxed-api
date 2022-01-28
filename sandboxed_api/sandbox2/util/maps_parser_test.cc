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

#include "sandboxed_api/sandbox2/util/maps_parser.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sandboxed_api/util/status_matchers.h"

namespace sandbox2 {
namespace {

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::Not;
using ::testing::Test;

TEST(MapsParserTest, ParsesValidFileCorrectly) {
  static constexpr char kValidMapsFile[] = R"ValidMapsFile(
555555554000-55555555c000 r-xp 00000000 fd:01 3277961                    /bin/cat
55555575b000-55555575c000 r--p 00007000 fd:01 3277961                    /bin/cat
55555575c000-55555575d000 rw-p 00008000 fd:01 3277961                    /bin/cat
55555575d000-55555577e000 rw-p 00000000 00:00 0                          [heap]
7ffff7a3a000-7ffff7bcf000 r-xp 00000000 fd:01 916748                     /lib/x86_64-linux-gnu/libc-2.24.so
7ffff7bcf000-7ffff7dcf000 ---p 00195000 fd:01 916748                     /lib/x86_64-linux-gnu/libc-2.24.so
7ffff7dcf000-7ffff7dd3000 r--p 00195000 fd:01 916748                     /lib/x86_64-linux-gnu/libc-2.24.so
7ffff7dd3000-7ffff7dd5000 rw-p 00199000 fd:01 916748                     /lib/x86_64-linux-gnu/libc-2.24.so
7ffff7dd5000-7ffff7dd9000 rw-p 00000000 00:00 0 
7ffff7dd9000-7ffff7dfc000 r-xp 00000000 fd:01 915984                     /lib/x86_64-linux-gnu/ld-2.24.so
7ffff7e2b000-7ffff7e7c000 r--p 00000000 fd:01 917362                     /usr/lib/locale/aa_DJ.utf8/LC_CTYPE
7ffff7e7c000-7ffff7fac000 r--p 00000000 fd:01 917355                     /usr/lib/locale/aa_DJ.utf8/LC_COLLATE
7ffff7fac000-7ffff7fae000 rw-p 00000000 00:00 0 
7ffff7fc1000-7ffff7fe3000 rw-p 00000000 00:00 0 
7ffff7fe3000-7ffff7fe4000 r--p 00000000 fd:01 920638                     /usr/lib/locale/aa_ET/LC_NUMERIC
7ffff7fe4000-7ffff7fe5000 r--p 00000000 fd:01 932780                     /usr/lib/locale/en_US.utf8/LC_TIME
7ffff7fe5000-7ffff7fe6000 r--p 00000000 fd:01 932409                     /usr/lib/locale/chr_US/LC_MONETARY
7ffff7fe6000-7ffff7fe7000 r--p 00000000 fd:01 932625                     /usr/lib/locale/en_AG/LC_MESSAGES/SYS_LC_MESSAGES
7ffff7fe7000-7ffff7fe8000 r--p 00000000 fd:01 932411                     /usr/lib/locale/chr_US/LC_PAPER
7ffff7fe8000-7ffff7fe9000 r--p 00000000 fd:01 932410                     /usr/lib/locale/chr_US/LC_NAME
7ffff7fe9000-7ffff7fea000 r--p 00000000 fd:01 932778                     /usr/lib/locale/en_US.utf8/LC_ADDRESS
7ffff7fea000-7ffff7feb000 r--p 00000000 fd:01 932412                     /usr/lib/locale/chr_US/LC_TELEPHONE
7ffff7feb000-7ffff7fec000 r--p 00000000 fd:01 932407                     /usr/lib/locale/chr_US/LC_MEASUREMENT
7ffff7fec000-7ffff7ff3000 r--s 00000000 fd:01 1179918                    /usr/lib/x86_64-linux-gnu/gconv/gconv-modules.cache
7ffff7ff3000-7ffff7ff4000 r--p 00000000 fd:01 932779                     /usr/lib/locale/en_US.utf8/LC_IDENTIFICATION
7ffff7ff4000-7ffff7ff7000 rw-p 00000000 00:00 0 
7ffff7ff7000-7ffff7ffa000 r--p 00000000 00:00 0                          [vvar]
7ffff7ffa000-7ffff7ffc000 r-xp 00000000 00:00 0                          [vdso]
7ffff7ffc000-7ffff7ffd000 r--p 00023000 fd:01 915984                     /lib/x86_64-linux-gnu/ld-2.24.so
7ffff7ffd000-7ffff7ffe000 rw-p 00024000 fd:01 915984                     /lib/x86_64-linux-gnu/ld-2.24.so
7ffff7ffe000-7ffff7fff000 rw-p 00000000 00:00 0 
7ffffffde000-7ffffffff000 rw-p 00000000 00:00 0                          [stack]
)ValidMapsFile";  // NOLINT
  SAPI_ASSERT_OK_AND_ASSIGN(std::vector<MapsEntry> entries,
                            ParseProcMaps(kValidMapsFile));
  EXPECT_THAT(entries.size(), Eq(32));
  EXPECT_THAT(entries[0].start, Eq(0x555555554000));
  EXPECT_THAT(entries[1].start, Eq(0x55555575b000));
  EXPECT_THAT(entries[1].end, Eq(0x55555575c000));
  EXPECT_THAT(entries[1].inode, Eq(3277961));

  EXPECT_THAT(entries[0].is_executable, Eq(true));
  EXPECT_THAT(entries[1].is_executable, Eq(false));
}

TEST(MapsParserTest, FailsOnInvalidFile) {
  static constexpr char kInvalidMapsFile[] = R"InvalidMapsFile(
555555554000-55555555c000 r-xp 00000000 fd:01 3277961                    /bin/cat
55555575b000-55555575c000 r--p 00007000 fd:01 3277961                    /bin/cat
55555575c000-55555575d000 rw-p 00008000 fd:01 3277961                    /bin/cat
55555575d000-55555577e000 rw-p 00000000 00:00 0                          [heap]
7ffff7fe4000+7ffff7fe5000 r--p 00000000 fdX01 932780                     /usr/lib/locale/en_US.utf8/LC_TIME
)InvalidMapsFile";
  auto status_or = ParseProcMaps(kInvalidMapsFile);
  ASSERT_THAT(status_or.status(), Not(IsOk()));
}

}  // namespace
}  // namespace sandbox2
