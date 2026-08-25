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
#ifndef SANDBOXED_API_TOOLS_CLANG_GENERATOR_ARG_H_
#define SANDBOXED_API_TOOLS_CLANG_GENERATOR_ARG_H_

#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "sandboxed_api/tools/clang_generator/sandboxed_library_emitter.h"

namespace sapi {

// Base class for different types of arguments.
// It provides an interface that allows to generate sandboxee/host wrappers
// with different number of actual arguments (e.g. a single argument
// in the original interface can be disassembled into/assembled from
// multiple arguments in the sandboxee wrapper interface)
// without knowing details of all possible argument types.
// NOTE: Classes in this hierarchy should not hold AST elements
// (like clang::QualType), as they will not be valid during Emit calls.
class SandboxedLibraryEmitter::Arg {
 public:
  Arg(absl::string_view name, absl::string_view type)
      : name_(name), type_(type) {}

  absl::string_view GetName() const { return name_; }

  const std::string& EmitRetType() const { return type_; }
  virtual std::string EmitHostParams() const {
    return absl::Substitute("$0 $1", type_, name_);
  }

  virtual std::vector<std::string> Includes() const { return {}; };
  virtual std::vector<std::string> HostStateVars() const { return {}; }
  virtual std::string EmitHostPreCall() const { return ""; }
  virtual std::string EmitHostPostCall() const { return ""; }
  virtual std::string EmitHostArgs() const = 0;
  virtual std::string EmitSandboxeeParams() const = 0;
  virtual std::string EmitSandboxeeArgs() const = 0;
  virtual std::string EmitSandboxeePreCall() const { return ""; }
  virtual std::string EmitSandboxeePostCall() const { return ""; }
  // TODO(dvyukov): Currently we pass return arguments as an additional argument
  // always to simplify the code. However, we could return scalar return values
  // directly since SAPI supports that, and that will be more efficient.
  virtual std::string EmitRetParams() const {
    LOG(FATAL) << "not implemented for " << name_ << " " << type_;
  }
  virtual std::string EmitRetPreCall() const {
    LOG(FATAL) << "not implemented for " << name_ << " " << type_;
  }
  virtual std::string EmitRetArgs() const {
    LOG(FATAL) << "not implemented for " << name_ << " " << type_;
  }
  virtual std::string EmitHostRet() const {
    LOG(FATAL) << "not implemented for " << name_ << " " << type_;
  }
  virtual std::string EmitSandboxeeRet() const {
    LOG(FATAL) << "not implemented for " << name_ << " " << type_;
  }
  virtual ~Arg() = default;

  // Search through and link any related arguments to this one, if needed.
  virtual absl::Status LinkArgsIfNeeded(const std::vector<ArgPtr>& args) {
    return absl::OkStatus();
  }

 protected:
  const std::string name_;
  const std::string type_;
};

}  // namespace sapi

#endif  // SANDBOXED_API_TOOLS_CLANG_GENERATOR_ARG_H_
