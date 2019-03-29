# Copyright 2019 Google LLC. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# On Debian, install libunwind-dev to use this module.

find_path(libunwind_INCLUDE_DIR sys/capability.h)
# Look for static library only.
find_library(libunwind_LIBRARY libcap.a)
mark_as_advanced(libunwind_INCLUDE_DIR libcap_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libunwind
  REQUIRED_VARS libunwind_LIBRARY libcap_INCLUDE_DIR
)

if(libunwind_FOUND AND NOT TARGET libcap::libcap)
  add_library(libunwind::libcap UNKNOWN IMPORTED)
  set_target_properties(libunwind::libcap PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    IMPORTED_LOCATION "${libunwind_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${libunwind_INCLUDE_DIR}"
  )
endif()
