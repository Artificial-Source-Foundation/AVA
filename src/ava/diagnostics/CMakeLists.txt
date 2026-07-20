add_library(ava_diagnostics
  "safe_failure.cpp"
)

target_include_directories(ava_diagnostics PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ava_diagnostics PUBLIC ava_core)
ava_apply_debugging(ava_diagnostics)
ava_apply_warnings(ava_diagnostics)
ava_apply_sanitizers(ava_diagnostics)
