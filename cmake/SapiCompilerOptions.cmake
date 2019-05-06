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

set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_SKIP_BUILD_RPATH TRUE)
set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

# Compiler-specific global options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")  # GCC
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
else()
  message(FATAL_ERROR "Unsupported compiler")
endif()

# OS-specific global options
if(UNIX)
  add_compile_options(-Wno-deprecated)
else()
  message(FATAL_ERROR "Unsupported OS")
endif()
