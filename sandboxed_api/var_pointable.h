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

#ifndef SANDBOXED_API_VAR_POINTABLE_H_
#define SANDBOXED_API_VAR_POINTABLE_H_

#include <memory>

#include "sandboxed_api/var_reg.h"

namespace sapi::v {

class Ptr;

// Needed so that we can use unique_ptr with incomplete type.
struct PtrDeleter {
  void operator()(Ptr* p);
};

// Class that implements pointer support for different objects.
class Pointable {
 public:
  enum SyncType {
    // Do not synchronize the underlying object after/before calls.
    SYNC_NONE = 0x0,
    // Synchronize the underlying object (send the data to the sandboxee)
    // before the call takes place.
    SYNC_BEFORE = 0x1,
    // Synchronize the underlying object (retrieve data from the sandboxee)
    // after the call has finished.
    SYNC_AFTER = 0x2,
    // Synchronize the underlying object with the remote object, by sending the
    // data to the sandboxee before the call, and retrieving it from the
    // sandboxee after the call has finished.
    SYNC_BOTH = SYNC_BEFORE | SYNC_AFTER,
  };

  // Necessary to implement creation of Ptr in inheriting class as it is
  // incomplete type here.
  virtual Ptr* CreatePtr(SyncType type) = 0;

  // Functions to get pointers with certain type of synchronization scheme.
  Ptr* PtrNone() {
    if (ptr_none_ == nullptr) {
      ptr_none_.reset(CreatePtr(SYNC_NONE));
    }

    return ptr_none_.get();
  }

  Ptr* PtrBoth() {
    if (ptr_both_ == nullptr) {
      ptr_both_.reset(CreatePtr(SYNC_BOTH));
    }

    return ptr_both_.get();
  }

  Ptr* PtrBefore() {
    if (ptr_before_ == nullptr) {
      ptr_before_.reset(CreatePtr(SYNC_BEFORE));
    }

    return ptr_before_.get();
  }

  Ptr* PtrAfter() {
    if (ptr_after_ == nullptr) {
      ptr_after_.reset(CreatePtr(SYNC_AFTER));
    }

    return ptr_after_.get();
  }

  virtual ~Pointable() = default;

 private:
  std::unique_ptr<Ptr, PtrDeleter> ptr_none_;
  std::unique_ptr<Ptr, PtrDeleter> ptr_both_;
  std::unique_ptr<Ptr, PtrDeleter> ptr_before_;
  std::unique_ptr<Ptr, PtrDeleter> ptr_after_;
};

}  // namespace sapi::v

#endif  // SANDBOXED_API_VAR_POINTABLE_H_
