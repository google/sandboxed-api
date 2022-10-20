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

#ifndef SANDBOXED_API_VAR_ARRAY_H_
#define SANDBOXED_API_VAR_ARRAY_H_

#include <algorithm>
#include <cstring>
#include <memory>

#include "absl/base/macros.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/util/status_macros.h"
#include "sandboxed_api/var_abstract.h"
#include "sandboxed_api/var_ptr.h"

namespace sapi::v {

// Class representing an array.
template <class T>
class Array : public Var {
 public:
  // The array is not owned by this object.
  Array(T* arr, size_t nelem)
      : arr_(arr),
        nelem_(nelem),
        total_size_(nelem_ * sizeof(T)),
        buffer_owned_(false) {
    SetLocal(const_cast<std::remove_const_t<T>*>(arr_));
  }

  // The array is allocated and owned by this object.
  explicit Array(size_t nelem)
      : nelem_(nelem), total_size_(nelem_ * sizeof(T)), buffer_owned_(true) {
    void* storage = malloc(sizeof(T) * nelem);
    CHECK(storage != nullptr);
    SetLocal(storage);
    arr_ = static_cast<T*>(storage);
  }

  virtual ~Array() {
    if (buffer_owned_) {
      free(const_cast<std::remove_const_t<T>*>(arr_));
    }
  }

  T& operator[](size_t v) const { return arr_[v]; }
  T* GetData() const { return arr_; }

  size_t GetNElem() const { return nelem_; }
  size_t GetSize() const final { return total_size_; }
  Type GetType() const final { return Type::kArray; }
  std::string GetTypeString() const final { return "Array"; }
  std::string ToString() const override {
    return absl::StrCat("Array, elem size: ", sizeof(T),
                        " B., total size: ", total_size_,
                        " B., nelems: ", GetNElem());
  }

  // Resizes the local and remote buffer using realloc(). Note that this will
  // make all pointers to the current data (inside and outside of the sandbox)
  // invalid.
  absl::Status Resize(RPCChannel* rpc_channel, size_t nelems) {
    size_t absolute_size = sizeof(T) * nelems;
    // Resize local buffer.
    SAPI_RETURN_IF_ERROR(EnsureOwnedLocalBuffer(absolute_size));

    // Resize remote buffer and update local pointer.
    void* new_addr;

    SAPI_RETURN_IF_ERROR(
        rpc_channel->Reallocate(GetRemote(), absolute_size, &new_addr));
    if (!new_addr) {
      return absl::UnavailableError("Reallocate() returned nullptr");
    }
    SetRemote(new_addr);
    return absl::OkStatus();
  }

 private:
  friend class LenVal;

  // Resizes the internal storage.
  absl::Status EnsureOwnedLocalBuffer(size_t size) {
    if (size % sizeof(T)) {
      return absl::FailedPreconditionError(
          "Array size not a multiple of the item size");
    }
    // Do not (re-)allocate memory if the new size matches our size - except
    // when we don't own that buffer.
    if (size == total_size_ && buffer_owned_) {
      return absl::OkStatus();
    }
    void* new_addr = nullptr;
    if (buffer_owned_) {
      new_addr = realloc(arr_, size);
    } else {
      new_addr = malloc(size);
      if (new_addr) {
        memcpy(new_addr, arr_, std::min(size, total_size_));
        buffer_owned_ = true;
      }
    }
    if (!new_addr) {
      return absl::UnavailableError("(Re-)malloc failed");
    }

    arr_ = static_cast<T*>(new_addr);
    total_size_ = size;
    nelem_ = size / sizeof(T);
    SetLocal(new_addr);
    return absl::OkStatus();
  }

  // Pointer to the data, owned by the object if buffer_owned_ is 'true'.
  T* arr_;
  size_t nelem_;       // Number of elements
  size_t total_size_;  // Total size in bytes
  bool buffer_owned_;  // Whether we own the buffer
};

// Specialized Array class for representing NUL-terminated C-style strings. The
// buffer is owned by the class, and is mutable.
class CStr : public Array<char> {
 public:
  explicit CStr(absl::string_view cstr) : Array<char>(cstr.size() + 1) {
    std::copy(cstr.begin(), cstr.end(), GetData());
    GetData()[cstr.size()] = '\0';
  }

  std::string ToString() const final {
    return absl::StrCat("CStr: len(w/o NUL):", strlen(GetData()), ", ['",
                        GetData(), "']");
  }
};

// Specialized Array class for representing NUL-terminated C-style strings. The
// buffer is not owned by the class and is not mutable.
class ConstCStr : public Array<const char> {
 public:
  explicit ConstCStr(const char* cstr)
      : Array<const char>(cstr, strlen(cstr) + 1) {}

  std::string ToString() const final {
    return absl::StrCat("ConstCStr: len(w/o NUL):", strlen(GetData()), ", ['",
                        GetData(), "']");
  }
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_ARRAY_H_
