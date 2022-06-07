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

#ifndef CONTRIB_LIBTIFF_TEST_CHECK_TAG_H_
#define CONTRIB_LIBTIFF_TEST_CHECK_TAG_H_

#include <cstdint>

#include "helper.h"  // NOLINT(build/include)
#include "tiffio.h"  // NOLINT(build/include)

void CheckShortField(TiffApi&, sapi::v::RemotePtr& tif, const ttag_t field,
                     const uint16_t value);
void CheckShortPairedField(TiffApi& api, sapi::v::RemotePtr& tif,
                           const ttag_t field,
                           const std::array<uint16_t, 2>& values);
void CheckLongField(TiffApi&, sapi::v::RemotePtr& tif, const ttag_t field,
                    const uint32_t value);

#endif  // CONTRIB_LIBTIFF_TEST_CHECK_TAG_H_
