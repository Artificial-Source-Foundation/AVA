add_library(ava_command
  "intent.cpp"
  "private_group.cpp"
  "discovery.cpp"
  "environment.cpp"
  "policy.cpp"
  "command_plan.cpp"
)

# Every implementation includes sys.h first; propagate the cwds include
# interface to all consumers of the public command.h debug contract.
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
