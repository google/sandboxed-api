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

// IMPORTANT: This file contains sandbox annotations for the lightweight
// sandboxing project that are planned but not implemented yet. It's mainly used
// to guide AI tools to generate code that can be used in the future.

#ifndef SANDBOXED_API_ANNOTATIONS_UNIMPLEMENTED_H_
#define SANDBOXED_API_ANNOTATIONS_UNIMPLEMENTED_H_

// The following annotations will be available in the future. If you need to use
// one of them, consider it unsupported and add an additional
// SANDBOX_UNSUPPORTED("reason") to the function declaration.
#define SANDBOX_UNSUPPORTED(reason) \
  [[clang::annotate("sandbox", "unsupported", reason)]]

// These need to be used if the sandbox allocates memory that is supposed to be
// freed by the host.
#define SANDBOX_MALLOCED [[clang::annotate("sandbox", "malloced")]]
#define SANDBOX_NEWED [[clang::annotate("sandbox", "newed")]]
#define SANDBOX_NEWED_ARRAY [[clang::annotate("sandbox", "newed_array")]]

// If an argument points into the data of another argument.
#define SANDBOX_INNER_PTR_OF(other_arg) \
  [[clang::annotate("sandbox", "inner_ptr_of", #other_arg)]]

// TODO(b/491762076): Annotates function pointers that are passed from the host
// to the sandboxee.
#define SANDBOX_CALLBACK [[clang::annotate("sandbox", "callback")]]

// TODO(b/491826252): Annotation for null-terminated strings.
#define SANDBOX_NULL_TERMINATED \
  [[clang::annotate("sandbox", "null_terminated")]]

// TODO(b/491826267): Annotation for return values that alias a parameter.
#define SANDBOX_ALIAS_PTR [[clang::annotate("sandbox", "alias_ptr")]]

// TODO(b/491828958): Annotation for when a function returns a pointer to a
// temporary buffer that is lifetime bound to another pointer.
#define SANDBOX_LIFETIME_BOUND(opaque_other) \
  [[clang::annotate("sandbox", "lifetime_bound", #opaque_other)]]

#endif  // SANDBOXED_API_ANNOTATIONS_UNIMPLEMENTED_H_
