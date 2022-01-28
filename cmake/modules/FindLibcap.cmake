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

# On Debian, install libcap-dev to use this module.

find_path(libcap_INCLUDE_DIR sys/capability.h)
# Look for static library only.
find_library(libcap_LIBRARY cap)
mark_as_advanced(libcap_INCLUDE_DIR libcap_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libcap
  REQUIRED_VARS libcap_LIBRARY libcap_INCLUDE_DIR
)

if(libcap_FOUND AND NOT TARGET libcap::libcap)
  add_library(libcap::libcap UNKNOWN IMPORTED)
  set_target_properties(libcap::libcap PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    IMPORTED_LOCATION "${libcap_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${libcap_INCLUDE_DIR}"
  )
endif()
