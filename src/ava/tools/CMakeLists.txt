add_library(ava_tools
  "bash_tool.cpp"
  "diff_utils.cpp"
  "edit_match.cpp"
  "file_io.cpp"
  "file_tools.cpp"
  "ignore_rules.cpp"
  "lsp_tools.cpp"
  "mutation_queue.cpp"
  "search_tools.cpp"
  "spill_files.cpp"
  "webfetch_tool.cpp"
  "websearch_tool.cpp"
)

target_include_directories(ava_tools PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ava_tools PUBLIC ava_observability)
ava_apply_debugging(ava_tools)
ava_apply_warnings(ava_tools)
ava_apply_sanitizers(ava_tools)
