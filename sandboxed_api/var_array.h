// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SANDBOXED_API_VAR_ARRAY_H_
#define SANDBOXED_API_VAR_ARRAY_H_

#include <cstring>
#include <memory>

#include "absl/base/macros.h"
#include "absl/strings/str_cat.h"
#include "sandboxed_api/rpcchannel.h"
#include "sandboxed_api/var_abstract.h"
#include "sandboxed_api/var_pointable.h"
#include "sandboxed_api/var_ptr.h"
#include "sandboxed_api/util/canonical_errors.h"
#include "sandboxed_api/util/status_macros.h"

namespace sapi::v {

// Class representing an array.
template <class T>
class Array : public Var, public Pointable {
 public:
  // The array is not owned by this object.
  Array(T* arr, size_t nelem)
      : arr_(arr),
        nelem_(nelem),
        total_size_(nelem_ * sizeof(T)),
        buffer_owned_(false) {
    SetLocal(const_cast<void*>(reinterpret_cast<const void*>(arr_)));
  }
  // The array is allocated and owned by this object.
  explicit Array(size_t nelem)
      : arr_(static_cast<T*>(malloc(sizeof(T) * nelem))),
        nelem_(nelem),
        total_size_(nelem_ * sizeof(T)),
        buffer_owned_(true) {
    SetLocal(const_cast<void*>(reinterpret_cast<const void*>(arr_)));
  }
  virtual ~Array() {
    if (buffer_owned_) {
      free(const_cast<void*>(reinterpret_cast<const void*>(arr_)));
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

  Ptr* CreatePtr(Pointable::SyncType type) override {
    return new Ptr(this, type);
  }

  // Resizes the local and remote buffer using realloc(). Note that this will
  // make all pointers to the current data (inside and outside of the sandbox)
  // invalid.
  sapi::Status Resize(RPCChannel* rpc_channel, size_t nelems) {
    size_t absolute_size = sizeof(T) * nelems;
    // Resize local buffer.
    SAPI_RETURN_IF_ERROR(EnsureOwnedLocalBuffer(absolute_size));

    // Resize remote buffer and update local pointer.
    void* new_addr;

    SAPI_RETURN_IF_ERROR(
        rpc_channel->Reallocate(GetRemote(), absolute_size, &new_addr));
    if (!new_addr) {
      return sapi::UnavailableError("Reallocate() returned nullptr");
    }
    SetRemote(new_addr);
    return sapi::OkStatus();
  }

 private:
  // Resizes the internal storage.
  sapi::Status EnsureOwnedLocalBuffer(size_t size) {
    if (size % sizeof(T)) {
      return sapi::FailedPreconditionError(
          "Array size not a multiple of the item size");
    }
    // Do not (re-)allocate memory if the new size matches our size - except
    // when we don't own that buffer.
    if (size == total_size_ && buffer_owned_) {
      return sapi::OkStatus();
    }
    void* new_addr = nullptr;
    if (buffer_owned_) {
      new_addr = realloc(arr_, size);
    } else {
      new_addr = malloc(size);
      if (new_addr) {
        memcpy(new_addr, arr_, total_size_);
        buffer_owned_ = true;
      }
    }
    if (!new_addr) {
      return sapi::UnavailableError("(Re-)malloc failed");
    }

    arr_ = static_cast<T*>(new_addr);
    total_size_ = size;
    nelem_ = size / sizeof(T);
    SetLocal(arr_);
    return sapi::OkStatus();
  }

  // Pointer to the data, owned by the object if buffer_owned_ is 'true'.
  T* arr_;
  // Number of elements.
  size_t nelem_;
  // Total size in bytes.
  size_t total_size_;
  // Do we own the buffer?
  bool buffer_owned_;

  friend class LenVal;
};

// Specialized Array class for representing NUL-terminated C-style strings. The
// buffer is owned by the class, and is mutable.
class CStr : public Array<char> {
 public:
  explicit CStr(char* cstr) : Array<char>(strlen(cstr) + 1) {
    strcpy(this->GetData(), cstr);  // NOLINT
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
    if (GetData() == nullptr) {
      return "CStr: [nullptr]";
    }
    return absl::StrCat("CStr: len(w/o NUL):", strlen(GetData()), ", ['",
                        GetData(), "']");
  }
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_ARRAY_H_
