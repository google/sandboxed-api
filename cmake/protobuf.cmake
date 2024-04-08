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

FetchContent_Declare(protobuf
  URL https://github.com/protocolbuffers/protobuf/releases/download/v26.1/protobuf-26.1.tar.gz  # 2024-03-27
  URL_HASH SHA256=4fc5ff1b2c339fb86cd3a25f0b5311478ab081e65ad258c6789359cd84d421f8
)

set(protobuf_ABSL_PROVIDER "package" CACHE STRING "" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(protobuf)

sapi_check_target(protobuf::libprotobuf)
sapi_check_target(protobuf::protoc)
set(Protobuf_INCLUDE_DIR "${protobuf_SOURCE_DIR}/src" CACHE INTERNAL "")
set(Protobuf_LIBRARIES protobuf::libprotobuf CACHE INTERNAL "")
