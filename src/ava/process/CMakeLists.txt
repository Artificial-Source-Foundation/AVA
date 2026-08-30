add_library(ava_process
  "owner.cpp"
  "supervisor.cpp"
  "types.cpp"
)

target_include_directories(ava_process
  PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(ava_process
  PUBLIC
    AVA::core
)

ava_apply_debugging(ava_process)
ava_apply_warnings(ava_process)
ava_apply_sanitizers(ava_process)

add_library(AVA::process ALIAS ava_process)
