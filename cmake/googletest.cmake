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

FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/334704df263b480a3e9e7441ed3292a5e30a37ec.zip  # 2023-06-06
  URL_HASH SHA256=a217118c2c36a3632b594af7ff98111a65bb2b980b726a7fa62305e02a998440
)
FetchContent_MakeAvailable(googletest)
