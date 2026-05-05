add_library(ava_lsp
  "lsp_client.cpp"
)

target_include_directories(ava_lsp PUBLIC "${CMAKE_SOURCE_DIR}/src")
ava_apply_warnings(ava_lsp)
ava_apply_sanitizers(ava_lsp)
