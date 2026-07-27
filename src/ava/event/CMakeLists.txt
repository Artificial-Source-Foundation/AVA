add_library(ava_event
  "RuntimeEvent.cpp"
  "events.cpp"
)

target_include_directories(ava_event PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(ava_event PUBLIC ava_core)
ava_apply_debugging(ava_event)
ava_apply_warnings(ava_event)
ava_apply_sanitizers(ava_event)
