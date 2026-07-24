add_library(ava_core
  "Application.cpp"
  "AnchorOpen.cpp"
  "AnchorSet.cpp"
  "atomic_file.cpp"
  "error.cpp"
  "ids.cpp"
  "json.cpp"
  "open_beneath.cpp"
  "path.cpp"
  "process_args.cpp"
  "strict_json.cpp"
  "string_utils.cpp"
  "trusted_home.cpp"
)

target_include_directories(ava_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ava_core PRIVATE nlohmann_json::nlohmann_json)
ava_apply_debugging(ava_core)
ava_apply_warnings(ava_core)
ava_apply_sanitizers(ava_core)
