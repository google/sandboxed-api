# Copyright 2019 Google LLC
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

# Downloads and unpacks glog at configure time

set(workdir "${CMAKE_BINARY_DIR}/glog-download")

configure_file("${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt.in"
               "${workdir}/CMakeLists.txt")
execute_process(COMMAND "${CMAKE_COMMAND}" -G "${CMAKE_GENERATOR}" .
                RESULT_VARIABLE error
                WORKING_DIRECTORY "${workdir}")
if(error)
  message(FATAL_ERROR "CMake step for ${PROJECT_NAME} failed: ${error}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" --build .
                RESULT_VARIABLE error
                WORKING_DIRECTORY "${workdir}")
if(error)
  message(FATAL_ERROR "Build step for ${PROJECT_NAME} failed: ${error}")
endif()

# Force gflags from subdirectory
set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
set(HAVE_LIB_GFLAGS 1)

set(UNWIND_LIBRARY FALSE)
set(HAVE_PWD_H FALSE)

add_subdirectory("${CMAKE_BINARY_DIR}/glog-src"
                 "${CMAKE_BINARY_DIR}/glog-build" EXCLUDE_FROM_ALL)
target_include_directories(glog PUBLIC
  $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/gflags-build/include>
)
