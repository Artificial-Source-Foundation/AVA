set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses QUIET)
if(NOT CURSES_FOUND)
  message(FATAL_ERROR "AVA requires ncursesw (wide-character ncurses) for ava_tui")
endif()

add_library(ava_tui
  "composer.cpp"
  "composer_diff.cpp"
  "composer_editor.cpp"
  "composer_input.cpp"
  "composer_palette.cpp"
  "composer_permission.cpp"
  "composer_select_list.cpp"
  "composer_text.cpp"
  "composer_transcript.cpp"
  "event_state.cpp"
  "keybindings.cpp"
  "runtime.cpp"
  "session_grants.cpp"
  "terminal.cpp"
  "terminal_image.cpp"
  "theme.cpp"
  "text.cpp"
  "text_wrap.cpp"
  "tool_cards.cpp"
)

target_link_libraries(ava_tui PRIVATE ${CURSES_LIBRARIES})
message(STATUS "AVA_GENERATED_INCLUDE_DIR = ${AVA_GENERATED_INCLUDE_DIR}")
target_include_directories(ava_tui
  PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${AVA_GENERATED_INCLUDE_DIR}"
  PRIVATE
  ${CURSES_INCLUDE_DIRS}
)
ava_apply_debugging(ava_tui)
ava_apply_warnings(ava_tui)
ava_apply_sanitizers(ava_tui)

add_subdirectory(terminal)
