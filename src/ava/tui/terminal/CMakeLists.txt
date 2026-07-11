# Create the `terminal` object library.
add_library(ava_terminal)

# Declare source files.
target_sources(ava_terminal
  PRIVATE
    "Session.cxx"
    "ComplexChar.cxx"
    "Window.cxx"
    "private_convert.cxx"
)

if (OptionEnableLibcwd)
target_sources(ava_terminal
  PRIVATE
    "print_members.cpp"
)
endif ()

# Set required include directories.
get_target_property(CWDS_INTERFACE_INCLUDE_DIRECTORIES AICxx::cwds INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(ava_terminal
  PUBLIC
    "${CWDS_INTERFACE_INCLUDE_DIRECTORIES}"     # For sys.h and debug.h.
    "${CMAKE_SOURCE_DIR}/src"                   # Because headers of terminal/ use #include "ava/...".
  INTERFACE
    "${CMAKE_SOURCE_DIR}/src/ava/tui"
  PRIVATE
    ${CURSES_INCLUDE_DIRS}
)

# Set link dependencies.
target_link_libraries(ava_terminal
  PUBLIC
    ${CURSES_LIBRARIES}
    ${AICXX_OBJECTS_LIST}
)

if (OptionEnableLibcwd)
  target_link_libraries(ava_terminal
    PUBLIC
      ava_debug
  )
endif ()

# Create an ALIAS target.
add_library(AVA::terminal ALIAS ava_terminal)
