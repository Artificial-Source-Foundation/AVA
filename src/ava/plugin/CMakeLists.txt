add_library(ava_plugin
  "diagnostics.cpp"
  "discovery.cpp"
  "enablement.cpp"
  "install.cpp"
  "manifest.cpp"
  "runner.cpp"
  "runner_io.cpp"
  "runner_process.cpp"
  "runner_protocol.cpp"
  "runner_support.cpp"
  "static_resources.cpp"
  "tool_broker.cpp"
  "ui_protocol.cpp"
)

target_include_directories(ava_plugin
  PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(ava_plugin
  PUBLIC
    AVA::context
    AVA::core
    AVA::process
    AVA::tools
  PRIVATE
    AVA::diagnostics
)

ava_apply_debugging(ava_plugin)
ava_apply_warnings(ava_plugin)
ava_apply_sanitizers(ava_plugin)

# Create an ALIAS target.
add_library(AVA::plugin ALIAS ava_plugin)
