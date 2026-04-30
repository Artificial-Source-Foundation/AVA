# Create the `terminal` object library.
add_library(terminal_ObjLib OBJECT)

# Declare source files.
target_sources(terminal_ObjLib
  PRIVATE
    "Session.cxx"
)

# Set required include directories.
target_include_directories(terminal_ObjLib
  PRIVATE
    "${CWDS_INTERFACE_INCLUDE_DIRECTORIES}"
)

# Set link dependencies.
target_link_libraries(terminal_ObjLib
  PUBLIC
    ${CURSES_LIBRARIES}
)

# Create an ALIAS target.
add_library(AVA::terminal ALIAS terminal_ObjLib)
