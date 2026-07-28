add_library(ava_mcp
  "config.cpp"
  "protocol.cpp"
  "stdio_client.cpp"
  "stdio_client_support.cpp"
  "tool_broker.cpp"
)

target_include_directories(ava_mcp PUBLIC "${CMAKE_SOURCE_DIR}/src" PRIVATE "${AVA_GENERATED_INCLUDE_DIR}")
target_link_libraries(ava_mcp PUBLIC ava_tools PRIVATE ava_diagnostics)
ava_apply_debugging(ava_mcp)
ava_apply_warnings(ava_mcp)
ava_apply_sanitizers(ava_mcp)
