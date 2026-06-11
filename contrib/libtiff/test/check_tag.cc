// Copyright 2020 Google LLC
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

#include "check_tag.h"  // NOLINT(build/include)

using ::sapi::IsOk;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;

// sapi functions:
// TIFFGetField

void CheckShortField(TiffApi& api, sapi::v::RemotePtr& tif, const ttag_t field,
                     const uint16_t value) {
  sapi::v::UShort tmp(123);
  absl::StatusOr<int> status_or_int;

  status_or_int = api.TIFFGetField1(&tif, field, tmp.PtrBoth());
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFGetField1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Problem fetching tag " << field;
  EXPECT_THAT(tmp.GetValue(), Eq(value))
      << "Wrong SHORT value fetched for tag " << field;
}

void CheckShortPairedField(TiffApi& api, sapi::v::RemotePtr& tif,
                           const ttag_t field,
                           const std::array<uint16_t, 2>& values) {
  sapi::v::UShort tmp0(123);
  sapi::v::UShort tmp1(456);
  absl::StatusOr<int> status_or_int;

  status_or_int =
      api.TIFFGetField2(&tif, field, tmp0.PtrBoth(), tmp1.PtrBoth());
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFGetField2 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Problem fetching tag " << field;
  EXPECT_THAT(tmp0.GetValue(), Eq(values[0]))
      << "Wrong SHORT PAIR[0] fetched for tag " << field;
  EXPECT_THAT(tmp1.GetValue(), Eq(values[1]))
      << "Wrong SHORT PAIR[1] fetched for tag " << field;
}

void CheckLongField(TiffApi& api, sapi::v::RemotePtr& tif, const ttag_t field,
                    const uint32_t value) {
  sapi::v::UInt tmp(123);
  absl::StatusOr<int> status_or_int;

  status_or_int = api.TIFFGetField1(&tif, field, tmp.PtrBoth());
  ASSERT_THAT(status_or_int, IsOk()) << "TIFFGetField1 fatal error";
  EXPECT_THAT(status_or_int.value(), IsTrue())
      << "Problem fetching tag " << field;
  EXPECT_THAT(tmp.GetValue(), Eq(value))
      << "Wrong LONG value fetched for tag " << field;
}
