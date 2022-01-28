# Copyright 2022 Google LLC
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

include(FetchContent)

if(CMAKE_VERSION VERSION_LESS 3.14)
  # Simple implementation for CMake 3.13, which is missing this.
  macro(FetchContent_MakeAvailable)
    foreach(content_name IN ITEMS ${ARGV})
      string(TOLOWER ${content_name} content_name_lower)
      FetchContent_GetProperties(${content_name})
      if(NOT ${content_name_lower}_POPULATED)
        FetchContent_Populate(${content_name})
        if(EXISTS ${${content_name_lower}_SOURCE_DIR}/CMakeLists.txt)
          add_subdirectory(${${content_name_lower}_SOURCE_DIR}
                           ${${content_name_lower}_BINARY_DIR})
        endif()
      endif()
    endforeach()
  endmacro()
endif()
