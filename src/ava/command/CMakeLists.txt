add_library(ava_command
  "command.cpp"
)

# command.cpp includes sys.h directly; propagate the cwds include interface to
# every consumer of the public command.h debug contract as well.
get_target_property(CWDS_INTERFACE_INCLUDE_DIRECTORIES AICxx::cwds INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(ava_command
  PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CWDS_INTERFACE_INCLUDE_DIRECTORIES}"
)
target_link_libraries(ava_command PUBLIC ava_core)
ava_apply_debugging(ava_command)
ava_apply_warnings(ava_command)
ava_apply_sanitizers(ava_command)
