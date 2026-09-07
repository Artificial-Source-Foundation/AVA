set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses QUIET)
if(NOT CURSES_FOUND)
  if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "AVA requires ncursesw (wide-character ncurses) for ava_tui; on macOS install Homebrew ncurses (brew install ncurses) which is keg-only under <prefix>/opt/ncurses")
  else()
    message(FATAL_ERROR "AVA requires ncursesw (wide-character ncurses) for ava_tui")
  endif()
endif()

add_library(ava_tui
  "command_output.cpp"
  "composer.cpp"
  "composer_diff.cpp"
  "composer_editor.cpp"
  "composer_input.cpp"
  "composer_palette.cpp"
  "composer_permission.cpp"
  "composer_plugin_ui.cpp"
  "composer_select_list.cpp"
  "composer_subagent_workspace.cpp"
  "composer_text.cpp"
  "composer_transcript.cpp"
  "event_state.cpp"
  "keybindings.cpp"
  "mermaid_projection.cpp"
  "prompt_stash_internal.cpp"
  "runtime.cpp"
  "runtime_active_run_internal.cpp"
  "runtime_active_run_input_internal.cpp"
  "runtime_active_run_input_preemptive_internal.cpp"
  "runtime_active_run_input_edit_internal.cpp"
  "runtime_active_run_input_mouse_internal.cpp"
  "runtime_actions_internal.cpp"
  "runtime_draft_internal.cpp"
  "runtime_render_internal.cpp"
  "runtime_prompts_internal.cpp"
  "runtime_plugin_ui_internal.cpp"
  "runtime_state.cpp"
  "runtime_submit_internal.cpp"
  "runtime_subagent_workspace_internal.cpp"
  "runtime_views.cpp"
  "runtime_commands.cpp"
  "runtime_input.cpp"
  "runtime_mermaid_internal.cpp"
  "runtime_navigation_internal.cpp"
  "runtime_transcript.cpp"
  "runtime_transcript_search_internal.cpp"
  "runtime_transcript_selection_internal.cpp"
  "runtime_user_turn_selection_internal.cpp"
  "session_grants.cpp"
  "terminal.cpp"
  "terminal_image.cpp"
  "theme.cpp"
  "text.cpp"
  "text_wrap.cpp"
  "tool_card_task_job.cpp"
  "tool_cards.cpp"
)

target_link_libraries(ava_tui
  PUBLIC
    AVA::core
    AVA::event
  PRIVATE
  ${CURSES_LIBRARIES}
)
# FindCurses reports required compiler flags (e.g. Homebrew ncursesw needs
# -DNCURSES_WIDECHAR and -D_DARWIN_C_SOURCE on macOS) via CURSES_CFLAGS.
if(CURSES_CFLAGS)
  target_compile_options(ava_tui PRIVATE ${CURSES_CFLAGS})
endif()
message(STATUS "AVA_GENERATED_INCLUDE_DIR = ${AVA_GENERATED_INCLUDE_DIR}")
target_include_directories(ava_tui
  PUBLIC
    "${CMAKE_SOURCE_DIR}/src"
    "${AVA_GENERATED_INCLUDE_DIR}"
  PRIVATE
     ${CURSES_INCLUDE_DIRS} ${NCURSES_INCLUDE_DIRS}
)
ava_apply_debugging(ava_tui)
ava_apply_warnings(ava_tui)
ava_apply_sanitizers(ava_tui)

add_subdirectory(terminal)

# Create an ALIAS target.
add_library(AVA::tui ALIAS ava_tui)
