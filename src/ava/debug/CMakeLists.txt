add_library(ava_debug
  "print_members_on.cpp"
)

target_include_directories(ava_debug PUBLIC "${CMAKE_SOURCE_DIR}/src")
ava_apply_debugging(ava_debug)
ava_apply_warnings(ava_debug)
ava_apply_sanitizers(ava_debug)
