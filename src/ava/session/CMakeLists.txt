add_library(ava_session
  "attachments.cpp"
  "compaction.cpp"
  "export.cpp"
  "record.cpp"
  "session_branch.cpp"
  "session_metadata.cpp"
  "session_store.cpp"
  "session_tree.cpp"
  "stats.cpp"
  "validation.cpp"
  "validation_fields.cpp"
)

target_include_directories(ava_session PUBLIC "${CMAKE_SOURCE_DIR}/src")
ava_apply_warnings(ava_session)
ava_apply_sanitizers(ava_session)
