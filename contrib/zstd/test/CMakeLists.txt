# SPDX-License-Identifier: Apache-2.0
# Copyright 2022 Google LLC
#                Mariusz Zaborski <oshogbo@invisiblethingslab.com>

include(GoogleTest)

add_executable(
  sapi_zstd_test

  zstd_test.cc
  ../utils/utils_zstd.cc
)


target_link_libraries(
  sapi_zstd_test PRIVATE

  sapi_zstd
  sapi::sapi
  sapi::temp_file
  gtest
  gtest_main
  gmock
)

gtest_discover_tests(sapi_zstd_test PROPERTIES ENVIRONMENT "TEST_FILES_DIR=${PROJECT_SOURCE_DIR}/files")
