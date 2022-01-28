# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# On Debian, install libffi-dev to use this module.

find_path(libffi_INCLUDE_DIR ffitarget.h)
# Look for static library only.
find_library(libffi_LIBRARY ffi)
mark_as_advanced(libffi_INCLUDE_DIR libffi_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libffi
  REQUIRED_VARS libffi_LIBRARY libffi_INCLUDE_DIR
)

if(libffi_FOUND AND NOT TARGET libffi::libffi)
  add_library(libffi::libffi UNKNOWN IMPORTED)
  set_target_properties(libffi::libffi PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    IMPORTED_LOCATION "${libffi_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${libffi_INCLUDE_DIR}"
  )
endif()
