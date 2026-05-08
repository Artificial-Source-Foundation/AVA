add_library(ava_context
  "context_loader.cpp"
  "skill_loader.cpp"
)

target_include_directories(ava_context PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ava_context PUBLIC ava_config ava_core)
ava_apply_warnings(ava_context)
ava_apply_sanitizers(ava_context)
