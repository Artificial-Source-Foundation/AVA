add_library(ava_permissions
  "permission.cpp"
  "permission_rule_codec.cpp"
  "permission_rule_file.cpp"
  "permission_rule_matching.cpp"
  "permission_rule_path.cpp"
  "permission_rules.cpp"
)

target_include_directories(ava_permissions
  PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(ava_permissions
  PUBLIC
    AVA::command
    AVA::core
)

ava_apply_debugging(ava_permissions)
ava_apply_warnings(ava_permissions)
ava_apply_sanitizers(ava_permissions)

# Create an ALIAS target.
add_library(AVA::permissions ALIAS ava_permissions)
