include(ExternalProject)

set_property(DIRECTORY PROPERTY EP_BASE Dependencies)

set(DEPENDENCIES)
set(EXTRA_CMAKE_ARGS)

ExternalProject_Add(absl
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
  GIT_TAG 88a152ae747c3c42dc9167d46c590929b048d436
  # Just clone into directory
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)
list(APPEND DEPENDENCIES absl)

ExternalProject_Add(gflags
  GIT_REPOSITORY https://github.com/gflags/gflags.git
  GIT_TAG 28f50e0fed19872e0fd50dd23ce2ee8cd759338e
  CMAKE_ARGS -DGFLAGS_IS_SUBPROJECT=TRUE
  INSTALL_COMMAND ""
)
list(APPEND DEPENDENCIES gflags)

ExternalProject_Add(glog
  DEPENDS gflags
  GIT_REPOSITORY https://github.com/google/glog.git
  GIT_TAG 41f4bf9cbc3e8995d628b459f6a239df43c2b84a
  CMAKE_ARGS
    # Disable symbolizer
    -DCMAKE_PREFIX_PATH= -DUNWIND_LIBRARY=
    # getpwuid_r() cannot be linked statically with glibc
    -DHAVE_PWD_H=
  INSTALL_COMMAND ""
)
list(APPEND DEPENDENCIES glog)

ExternalProject_Add(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG f80d6644d4b451f568a2e7aea1e01e842eb242dc  # 2019-02-05
  # Just clone into directory
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)
list(APPEND DEPENDENCIES googletest)

ExternalProject_Add(libunwind
  URL https://github.com/libunwind/libunwind/releases/download/v1.2.1/libunwind-1.2.1.tar.gz
  URL_HASH SHA256=3f3ecb90e28cbe53fba7a4a27ccce7aad188d3210bb1964a923a731a27a75acb
  # Need to invoke a custom build, so just download and extract
  CONFIGURE_COMMAND ./configure
                    --disable-documentation
                    --disable-minidebuginfo
                    --disable-shared
                    --enable-ptrace
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
  BUILD_IN_SOURCE TRUE
)
list(APPEND DEPENDENCIES libunwind)

ExternalProject_Add(sandboxed_api
  DEPENDS ${DEPENDENCIES}
  SOURCE_DIR ${PROJECT_SOURCE_DIR}
  CMAKE_ARGS -DUSE_SUPERBUILD=OFF ${EXTRA_CMAKE_ARGS}
  INSTALL_COMMAND ""
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}
)
