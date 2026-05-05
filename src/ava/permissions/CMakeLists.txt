add_library(ava_permissions
  "permission.cpp"
)

target_include_directories(ava_permissions PUBLIC "${CMAKE_SOURCE_DIR}/src")
ava_apply_warnings(ava_permissions)
ava_apply_sanitizers(ava_permissions)
