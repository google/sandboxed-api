
include(GoogleTest)
enable_testing()

add_executable(sapi_minitar_test
    minitar_test.cc
)

target_link_libraries(sapi_minitar_test PRIVATE
  sapi_minitar_lib
  gtest
  gmock
  sapi::test_main
)

gtest_discover_tests(sapi_minitar_test)
