
#target_include_directories(jsonnet_helper PUBLIC
#  "${CMAKE_CURRENT_SOURCE_DIR}/jsonnet"
#  "${PROJECT_BINARY_DIR}"
#)

add_executable(jsonnet_sandboxed
    jsonnet_example.cc
)

target_link_libraries(jsonnet_sandboxed PRIVATE
  libjsonnet
  jsonnet_helper
  jsonnet_sapi
  sapi::sapi
)
