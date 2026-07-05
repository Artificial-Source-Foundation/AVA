add_library(ava_permissions
  "permission.cpp"
  "permission_rules.cpp"
)

target_include_directories(ava_permissions PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ava_permissions PUBLIC ava_core)
ava_apply_debugging(ava_permissions)
ava_apply_warnings(ava_permissions)
ava_apply_sanitizers(ava_permissions)
