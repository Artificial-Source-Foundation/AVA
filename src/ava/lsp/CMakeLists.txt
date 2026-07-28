add_library(ava_lsp
  "bounded_file_reader.cpp"
  "builtin_recipes.cpp"
  "configured_provider.cpp"
  "lsp_client.cpp"
  "lsp_process.cpp"
  "lsp_protocol.cpp"
  "lsp_transport.cpp"
)

target_include_directories(ava_lsp PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ava_lsp PUBLIC ava_core PRIVATE ava_command)
ava_apply_debugging(ava_lsp)
ava_apply_warnings(ava_lsp)
ava_apply_sanitizers(ava_lsp)
