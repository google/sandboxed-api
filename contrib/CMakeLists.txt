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

# Append to this list whenever a new sandboxed library is added to `contrib/`.
set(SAPI_CONTRIB_SANDBOXES
  hunspell
  jsonnet
  libidn2
  pffft
  turbojpeg
  zopfli
  zstd
)

foreach(_contrib IN LISTS SAPI_CONTRIB_SANDBOXES)
  add_sapi_subdirectory("contrib/${_contrib}" INCLUDE_FROM_ALL)
endforeach()
