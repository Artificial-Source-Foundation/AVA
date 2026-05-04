cmake_minimum_required(VERSION 3.25)

project(ava VERSION 0.32.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(AVA_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(AVA_VERSION_DISPLAY "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
file(MAKE_DIRECTORY "${AVA_GENERATED_INCLUDE_DIR}/ava/core")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ava/core/version.h.in"
  "${AVA_GENERATED_INCLUDE_DIR}/ava/core/version.h"
  @ONLY
)

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
  src/ava/agent/assistant_turn.cpp
  src/ava/agent/message_builder.cpp
  src/ava/agent/mode.cpp
  src/ava/agent/provider_output_validation.cpp
  src/ava/agent/question.cpp
  src/ava/agent/stream_bridge.cpp
  src/ava/agent/tool_dispatcher.cpp
  src/ava/agent/tool_registry.cpp
  src/ava/agent/tool_result.cpp
  src/ava/agent/tool_summaries.cpp
  src/ava/agent/usage_accounting.cpp
  src/ava/config/auth.cpp
  src/ava/config/model_config.cpp
  src/ava/config/model_profiles.cpp
  src/ava/config/openai_oauth.cpp
  src/ava/config/provider_profiles.cpp
  src/ava/config/prompt_config.cpp
  src/ava/config/reasoning_profiles.cpp
  src/ava/config/xdg_paths.cpp
  src/ava/context/context_loader.cpp
  src/ava/core/error.cpp
  src/ava/core/ids.cpp
  src/ava/core/json.cpp
  src/ava/lsp/lsp_client.cpp
  src/ava/mcp/config.cpp
  src/ava/mcp/stdio_client.cpp
  src/ava/mcp/tool_broker.cpp
  src/ava/permissions/permission.cpp
  src/ava/plugin/diagnostics.cpp
  src/ava/plugin/discovery.cpp
  src/ava/plugin/enablement.cpp
  src/ava/plugin/manifest.cpp
  src/ava/plugin/runner.cpp
  src/ava/plugin/tool_broker.cpp
  src/ava/provider/curl_transport.cpp
  src/ava/provider/anthropic_provider.cpp
  src/ava/provider/openai_compatible_provider.cpp
  src/ava/provider/openai_provider.cpp
  src/ava/provider/openai_request.cpp
  src/ava/provider/openai_response_parser.cpp
  src/ava/provider/openai_stream_parser.cpp
  src/ava/provider/provider.cpp
  src/ava/provider/provider_utils.cpp
  src/ava/provider/registry.cpp
  src/ava/session/compaction.cpp
  src/ava/session/export.cpp
  src/ava/session/session_store.cpp
  src/ava/session/stats.cpp
  src/ava/session/validation.cpp
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

target_include_directories(ava_core PUBLIC src "${AVA_GENERATED_INCLUDE_DIR}")
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
  src/ava/tui/text.cpp
  src/ava/tui/tool_cards.cpp
)

target_link_libraries(ava_tui PUBLIC ava_core PRIVATE ${CURSES_LIBRARIES})
target_include_directories(ava_tui PUBLIC src PRIVATE ${CURSES_INCLUDE_DIRS})
ava_apply_warnings(ava_tui)
ava_apply_sanitizers(ava_tui)

add_library(ava_app
  src/ava/app/app.cpp
  src/ava/app/command_catalog.cpp
  src/ava/app/command_connect.cpp
  src/ava/app/command_format.cpp
  src/ava/app/command_help.cpp
  src/ava/app/command_mcp.cpp
  src/ava/app/command_models.cpp
  src/ava/app/command_palette.cpp
  src/ava/app/command_plugins.cpp
  src/ava/app/command_sessions.cpp
  src/ava/app/command_tools.cpp
  src/ava/app/commands.cpp
  src/ava/app/connect_openai.cpp
  src/ava/app/events.cpp
  src/ava/app/headless_policy.cpp
  src/ava/app/interactive_run_queue.cpp
  src/ava/app/line_shell.cpp
  src/ava/app/plugin_event_hooks.cpp
  src/ava/app/print_mode.cpp
  src/ava/app/reasoning_controls.cpp
  src/ava/app/rpc/handlers.cpp
  src/ava/app/rpc/output.cpp
  src/ava/app/rpc/protocol.cpp
  src/ava/app/rpc/resolvers.cpp
  src/ava/app/rpc/run_state.cpp
  src/ava/app/rpc/serialization.cpp
  src/ava/app/rpc_mode.cpp
  src/ava/app/runtime.cpp
  src/ava/app/runtime_compaction.cpp
  src/ava/app/runtime_json.cpp
  src/ava/app/runtime_model.cpp
  src/ava/app/runtime_prompt.cpp
  src/ava/app/runtime_reasoning.cpp
  src/ava/app/runtime_retry.cpp
  src/ava/app/runtime_session.cpp
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

  add_executable(ava_fake_mcp_server tests/support/fake_mcp_server.cpp)
  target_link_libraries(ava_fake_mcp_server PRIVATE ava_core)
  ava_apply_warnings(ava_fake_mcp_server)
  ava_apply_sanitizers(ava_fake_mcp_server)

  add_executable(ava_tests
    tests/agent_tool_dispatcher_tests.cpp
    tests/app_runtime_tests.cpp
    tests/config_context_auth_oauth_tests.cpp
    tests/core_tests.cpp
    tests/core_mode_json_permissions_tests.cpp
    tests/events_tests.cpp
    tests/lsp_tests.cpp
    tests/mcp_tests.cpp
    tests/plugin_tests.cpp
    tests/provider_anthropic_tests.cpp
    tests/provider_openai_tests.cpp
    tests/session_tests.cpp
    tests/tools_tests.cpp
    tests/tui_composer_tests.cpp
  )
  target_link_libraries(ava_tests PRIVATE ava_core ava_tui ava_app ava_test_support)
  add_dependencies(ava_tests ava_fake_lsp_server ava_fake_mcp_server)
  target_compile_definitions(ava_tests PRIVATE AVA_FAKE_LSP_SERVER_PATH="$<TARGET_FILE:ava_fake_lsp_server>")
  target_compile_definitions(ava_tests PRIVATE AVA_FAKE_MCP_SERVER_PATH="$<TARGET_FILE:ava_fake_mcp_server>")
  ava_apply_warnings(ava_tests)
  ava_apply_sanitizers(ava_tests)

  set(AVA_TEST_SUITES
    core_mode
    session
    core_json_permission
    app_command_classification
    tools
    config_context_auth_oauth
    app_event_serialization
    app_event_bus
    app_runtime
    provider_openai
    provider_anthropic
    agent_tool_dispatcher
    lsp
    plugin
    mcp
    tui_composer
  )
  foreach(AVA_TEST_SUITE IN LISTS AVA_TEST_SUITES)
    add_test(NAME ava_tests.${AVA_TEST_SUITE} COMMAND ava_tests ${AVA_TEST_SUITE})
  endforeach()

  add_test(
    NAME ava_cli.headless_rpc_validate_session
    COMMAND ${CMAKE_COMMAND}
            -DAVA_EXE=$<TARGET_FILE:ava>
            -DAVA_CLI_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-headless-rpc-validate
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_headless_rpc_validate.cmake)

  add_test(
    NAME ava_cli.headless_rpc_contract
    COMMAND ${CMAKE_COMMAND}
            -DAVA_EXE=$<TARGET_FILE:ava>
            -DAVA_CLI_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-headless-rpc-contract
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_headless_rpc_contract.cmake)

  add_test(
    NAME ava_cli.headless_rpc_cancel
    COMMAND ${CMAKE_COMMAND}
            -DAVA_EXE=$<TARGET_FILE:ava>
            -DAVA_CLI_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-headless-rpc-cancel
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_headless_rpc_cancel.cmake)

  add_test(
    NAME ava_cli.headless_rpc_error_recovery
    COMMAND ${CMAKE_COMMAND}
            -DAVA_EXE=$<TARGET_FILE:ava>
            -DAVA_CLI_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-headless-rpc-error-recovery
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_headless_rpc_error_recovery.cmake)

  add_test(
    NAME ava_cli.headless_rpc_mcp_commands
    COMMAND ${CMAKE_COMMAND}
            -DAVA_EXE=$<TARGET_FILE:ava>
            -DAVA_FAKE_MCP_SERVER_EXE=$<TARGET_FILE:ava_fake_mcp_server>
            -DAVA_CLI_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-headless-rpc-mcp-commands
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_headless_rpc_mcp_commands.cmake)
endif()
