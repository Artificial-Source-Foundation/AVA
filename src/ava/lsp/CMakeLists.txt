add_library(ava_lsp
  "bounded_file_reader.cpp"
  "builtin_recipes.cpp"
  "configured_provider.cpp"
  "lsp_client.cpp"
)

target_include_directories(ava_lsp PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ava_lsp PRIVATE ava_command)
ava_apply_debugging(ava_lsp)
ava_apply_warnings(ava_lsp)
ava_apply_sanitizers(ava_lsp)
