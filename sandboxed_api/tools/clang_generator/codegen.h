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

#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_CODEGEN_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_CODEGEN_H_

#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "sandboxed_api/tools/clang_generator/generator.h"
#include "sandboxed_api/tools/clang_generator/ir.h"

namespace sapi {

// Emits the C function prototype for `func` as called by host code.
void EmitFuncDecl(std::string& out, const ir::Function& func);

// Emits the internal sandboxee wrapper prototype for `func`.
void EmitWrapperDecl(std::string& out, const ir::Function& func);

// Generates the sandboxee C++ header declaring wrapper prototypes and
// library dependencies for sandbox execution.
absl::StatusOr<std::string> EmitSandboxeeHdr(
    const GeneratorOptions& options, const ir::Library& library,
    const absl::flat_hash_set<std::string>& includes);

// Generates the sandboxee C++ source implementing RPC wrapper thunks that
// unpack arguments and invoke the target sandboxed library functions.
absl::StatusOr<std::string> EmitSandboxeeSrc(
    const GeneratorOptions& options, const ir::Library& library,
    const absl::flat_hash_set<std::string>& includes);

// Generates a stub `main()` that references every wrapped library function so
// the linker retains their symbols for static and syscall analysis.
absl::StatusOr<std::string> EmitSandboxeeMain(const GeneratorOptions& options,
                                              const ir::Library& library);

// Generates the host-side C++ source defining the SAPI Sandbox class,
// marshaling data across process boundaries, handling callbacks, and syncing
// buffers.
absl::StatusOr<std::string> EmitHostSrc(
    const GeneratorOptions& options, const ir::Library& library,
    const absl::flat_hash_set<std::string>& includes);

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_CODEGEN_H_
