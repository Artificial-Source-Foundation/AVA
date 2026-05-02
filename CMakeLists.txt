cmake_minimum_required(VERSION 3.25)

project(ava VERSION 0.32.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AVA_BUILD_TESTS "Build AVA tests" ON)
option(AVA_ENABLE_SANITIZERS "Build AVA with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses QUIET)
if(NOT CURSES_FOUND)
  message(FATAL_ERROR "AVA requires ncursesw (wide-character ncurses) for ava_tui")
endif()

function(ava_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

function(ava_apply_sanitizers target)
  if(AVA_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

add_library(ava_core
  src/ava/agent/agent_loop.cpp
  src/ava/agent/mode.cpp
  src/ava/agent/question.cpp
  src/ava/agent/tool_dispatcher.cpp
  src/ava/config/auth.cpp
  src/ava/config/model_config.cpp
  src/ava/config/openai_oauth.cpp
  src/ava/config/prompt_config.cpp
  src/ava/config/xdg_paths.cpp
  src/ava/context/context_loader.cpp
  src/ava/core/error.cpp
  src/ava/core/ids.cpp
  src/ava/core/json.cpp
  src/ava/lsp/lsp_client.cpp
  src/ava/permissions/permission.cpp
  src/ava/provider/curl_transport.cpp
  src/ava/provider/anthropic_provider.cpp
  src/ava/provider/openai_provider.cpp
  src/ava/provider/provider.cpp
  src/ava/provider/provider_utils.cpp
  src/ava/provider/registry.cpp
  src/ava/session/compaction.cpp
  src/ava/session/export.cpp
  src/ava/session/session_store.cpp
  src/ava/session/stats.cpp
  src/ava/tools/bash_tool.cpp
  src/ava/tools/diff_utils.cpp
  src/ava/tools/edit_match.cpp
  src/ava/tools/file_tools.cpp
  src/ava/tools/ignore_rules.cpp
  src/ava/tools/lsp_tools.cpp
  src/ava/tools/mutation_queue.cpp
  src/ava/tools/search_tools.cpp
  src/ava/tools/spill_files.cpp
  src/ava/tools/webfetch_tool.cpp
)

target_include_directories(ava_core PUBLIC src)
ava_apply_warnings(ava_core)
ava_apply_sanitizers(ava_core)

add_library(ava_tui
  src/ava/tui/composer.cpp
  src/ava/tui/composer_editor.cpp
  src/ava/tui/composer_input.cpp
  src/ava/tui/composer_palette.cpp
  src/ava/tui/composer_permission.cpp
  src/ava/tui/composer_text.cpp
  src/ava/tui/composer_transcript.cpp
  src/ava/tui/event_state.cpp
  src/ava/tui/keybindings.cpp
  src/ava/tui/runtime.cpp
  src/ava/tui/terminal.cpp
)

target_link_libraries(ava_tui PUBLIC ava_core PRIVATE ${CURSES_LIBRARIES})
target_include_directories(ava_tui PUBLIC src PRIVATE ${CURSES_INCLUDE_DIRS})
ava_apply_warnings(ava_tui)
ava_apply_sanitizers(ava_tui)

add_library(ava_app
  src/ava/app/app.cpp
  src/ava/app/command_catalog.cpp
  src/ava/app/commands.cpp
  src/ava/app/connect_openai.cpp
  src/ava/app/events.cpp
  src/ava/app/headless_policy.cpp
  src/ava/app/line_shell.cpp
  src/ava/app/print_mode.cpp
  src/ava/app/rpc_mode.cpp
  src/ava/app/runtime.cpp
)

target_link_libraries(ava_app PUBLIC ava_core ava_tui)
target_include_directories(ava_app PUBLIC src)
ava_apply_warnings(ava_app)
ava_apply_sanitizers(ava_app)

add_executable(ava src/main.cpp)
target_link_libraries(ava PRIVATE ava_app)
ava_apply_warnings(ava)
ava_apply_sanitizers(ava)

if(AVA_BUILD_TESTS)
  enable_testing()
  add_library(ava_test_support
    tests/support/fake_transport.cpp
    tests/support/test_harness.cpp
  )
  target_link_libraries(ava_test_support PUBLIC ava_core)
  target_include_directories(ava_test_support PUBLIC .)
  ava_apply_warnings(ava_test_support)
  ava_apply_sanitizers(ava_test_support)

  add_executable(ava_fake_lsp_server tests/support/fake_lsp_server.cpp)
  target_link_libraries(ava_fake_lsp_server PRIVATE ava_core)
  ava_apply_warnings(ava_fake_lsp_server)
  ava_apply_sanitizers(ava_fake_lsp_server)

  add_executable(ava_tests
    tests/agent_tool_dispatcher_tests.cpp
    tests/app_runtime_tests.cpp
    tests/config_context_auth_oauth_tests.cpp
    tests/core_tests.cpp
    tests/core_mode_json_permissions_tests.cpp
    tests/events_tests.cpp
    tests/lsp_tests.cpp
    tests/provider_anthropic_tests.cpp
    tests/provider_openai_tests.cpp
    tests/session_tests.cpp
    tests/tools_tests.cpp
    tests/tui_composer_tests.cpp
  )
  target_link_libraries(ava_tests PRIVATE ava_core ava_tui ava_app ava_test_support)
  add_dependencies(ava_tests ava_fake_lsp_server)
  target_compile_definitions(ava_tests PRIVATE AVA_FAKE_LSP_SERVER_PATH="$<TARGET_FILE:ava_fake_lsp_server>")
  ava_apply_warnings(ava_tests)
  ava_apply_sanitizers(ava_tests)
  add_test(NAME ava_tests COMMAND ava_tests)
endif()
