# SPDX-License-Identifier: Apache-2.0
# Copyright 2022 Google LLC
#                Mariusz Zaborski <oshogbo@invisiblethingslab.com>

add_executable(
  sapi_minizstd

  main.cc
  ../utils/utils_zstd.cc
)

target_link_libraries(
  sapi_minizstd PRIVATE

  sapi_zstd
  sapi::sapi
)
