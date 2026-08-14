add_library(ava_containment
  "containment_plan.cpp"
  "landlock.cpp"
  "seccomp_network.cpp"
)

get_target_property(CWDS_INTERFACE_INCLUDE_DIRECTORIES AICxx::cwds INTERFACE_INCLUDE_DIRECTORIES)

target_include_directories(ava_containment
  PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${CWDS_INTERFACE_INCLUDE_DIRECTORIES}"
)

target_link_libraries(ava_containment
  PUBLIC
    AVA::command
    AVA::core
)

ava_apply_debugging(ava_containment)
ava_apply_warnings(ava_containment)
ava_apply_sanitizers(ava_containment)

# Create an ALIAS target.
add_library(AVA::containment ALIAS ava_containment)
