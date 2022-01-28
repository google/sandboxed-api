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

#ifndef SANDBOXED_API_TYPE_H_
#define SANDBOXED_API_TYPE_H_

#include <ostream>

namespace sapi::v {

enum class Type {
  kVoid,     // Void
  kInt,      // Intergal types
  kFloat,    // Floating-point types
  kPointer,  // Pointer to an arbitrary data type
  kStruct,   // Structures
  kArray,    // Arrays, memory buffers
  kFd,       // File descriptors/handles
  kProto,    // Protocol buffers
  kLenVal,   // Dynamic buffers
};

inline std::ostream& operator<<(std::ostream& os, const Type& type) {
  return os << static_cast<int>(type);
}

}  // namespace sapi::v

#endif  // SANDBOXED_API_TYPE_H_
