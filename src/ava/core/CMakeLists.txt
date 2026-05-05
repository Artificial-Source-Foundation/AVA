add_library(ava_core
  "error.cpp"
  "ids.cpp"
  "json.cpp"
)

target_include_directories(ava_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
ava_apply_warnings(ava_core)
ava_apply_sanitizers(ava_core)
