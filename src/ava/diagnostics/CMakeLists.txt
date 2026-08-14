add_library(ava_diagnostics
  "artifact_store.cpp"
  "records.cpp"
  "runtime_diagnostics.cpp"
  "safe_failure.cpp"
)

target_include_directories(ava_diagnostics
  PUBLIC "${CMAKE_SOURCE_DIR}/src"
  PRIVATE "${AVA_GENERATED_INCLUDE_DIR}"
)

target_link_libraries(ava_diagnostics
  PUBLIC
    AVA::core
  PRIVATE
    AVA::config
    AVA::observability
    nlohmann_json::nlohmann_json
)

ava_apply_debugging(ava_diagnostics)
ava_apply_warnings(ava_diagnostics)
ava_apply_sanitizers(ava_diagnostics)

# Create an ALIAS target.
add_library(AVA::diagnostics ALIAS ava_diagnostics)
