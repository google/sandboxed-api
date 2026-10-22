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

// A binary that sends a malformed version string to the sandbox host instead
// of performing the regular Client handshake, then exits.
// It is used to test that the host handles unparsable versions gracefully.

#include <cstdlib>

#include "absl/strings/string_view.h"
#include "sandboxed_api/sandbox2/comms.h"

int main(int argc, char* argv[]) {
  sandbox2::Comms comms(sandbox2::Comms::kDefaultConnection);
  // Contains a 'V' separator, but the version number is not an integer.
  constexpr absl::string_view kMalformedVersion = "Vmalformed";
  if (!comms.SendTLV(sandbox2::Comms::kTagVersion, kMalformedVersion.size(),
                     kMalformedVersion.data())) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
