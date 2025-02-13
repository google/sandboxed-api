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

"""General build definitions useful for the whole project."""

_SAPI_LINUX_COPTS = [
    "-Wno-deprecated-declarations",
    "-Wno-narrowing",
    "-Wno-sign-compare",
    "-Wunused-result",
]

def sapi_platform_copts(copts = []):
    """Returns the default compiler options for the current platform.

    Args:
      copts: additional compiler options to include.
    """

    # Linux only for now.
    return _SAPI_LINUX_COPTS + copts
