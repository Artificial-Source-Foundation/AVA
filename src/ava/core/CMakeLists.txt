add_library(ava_core
  "atomic_file.cpp"
  "error.cpp"
  "ids.cpp"
  "json.cpp"
  "process_args.cpp"
  "strict_json.cpp"
)

target_include_directories(ava_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ava_core PRIVATE nlohmann_json::nlohmann_json)
ava_apply_debugging(ava_core)
ava_apply_warnings(ava_core)
ava_apply_sanitizers(ava_core)
