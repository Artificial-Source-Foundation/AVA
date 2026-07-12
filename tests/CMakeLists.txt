set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

add_library(ava_test_support
  "support/app_runtime_support.cpp"
  "support/fake_transport.cpp"
  "support/test_harness.cpp"
)
target_link_libraries(ava_test_support PUBLIC ${AVA_BACKEND_LIBRARIES})
target_include_directories(ava_test_support PUBLIC ${CMAKE_SOURCE_DIR})
ava_apply_warnings(ava_test_support)
ava_apply_sanitizers(ava_test_support)

add_executable(ava_fake_lsp_server "support/fake_lsp_server.cpp")
target_link_libraries(ava_fake_lsp_server PRIVATE ${AVA_BACKEND_LIBRARIES})
ava_apply_warnings(ava_fake_lsp_server)
ava_apply_sanitizers(ava_fake_lsp_server)

add_executable(ava_fake_mcp_server "support/fake_mcp_server.cpp")
target_link_libraries(ava_fake_mcp_server PRIVATE ${AVA_BACKEND_LIBRARIES})
ava_apply_warnings(ava_fake_mcp_server)
ava_apply_sanitizers(ava_fake_mcp_server)

add_executable(ava_fake_provider_server "support/fake_provider_server.cpp")
ava_apply_warnings(ava_fake_provider_server)
ava_apply_sanitizers(ava_fake_provider_server)

add_executable(ava_tests
  "agent_loop_resilience_tests.cpp"
  "agent_loop_tests.cpp"
  "agent_tool_dispatcher_tests.cpp"
  "app_compaction_tests.cpp"
  "app_command_registry_tests.cpp"
  "app_print_tests.cpp"
  "app_rpc_queue_tests.cpp"
  "app_rpc_resolver_tests.cpp"
  "app_rpc_tests.cpp"
  "app_runtime_tests.cpp"
  "config_context_auth_oauth_tests.cpp"
  "core_tests.cpp"
  "core_mode_json_permissions_tests.cpp"
  "events_tests.cpp"
  "lsp_tests.cpp"
  "mcp_tests.cpp"
  "permission_rules_tests.cpp"
  "plugin_tests.cpp"
  "provider_anthropic_tests.cpp"
  "provider_gemini_tests.cpp"
  "provider_live_smoke_tests.cpp"
  "provider_openai_tests.cpp"
  "session_tests.cpp"
  "tool_scheduler_tests.cpp"
  "tools_file_tests.cpp"
  "tools_process_network_tests.cpp"
  "tools_search_tests.cpp"
  "tools_tests.cpp"
  "tui_composer_tests.cpp"
  "run_observer_tests.cpp"
)
target_link_libraries(ava_tests PRIVATE ${AVA_FRONTEND_LIBRARIES} ${AVA_BACKEND_LIBRARIES} ava_test_support ${AICXX_OBJECTS_LIST})
target_include_directories(ava_tests PRIVATE ${CURSES_INCLUDE_DIRS})
add_dependencies(ava_tests ava_fake_lsp_server ava_fake_mcp_server ava_fake_provider_server)
target_compile_definitions(ava_tests PRIVATE AVA_FAKE_LSP_SERVER_PATH="$<TARGET_FILE:ava_fake_lsp_server>")
target_compile_definitions(ava_tests PRIVATE AVA_FAKE_MCP_SERVER_PATH="$<TARGET_FILE:ava_fake_mcp_server>")
target_compile_definitions(ava_tests PRIVATE AVA_SAMPLE_TODO_PLUGIN_DIR="${CMAKE_SOURCE_DIR}/examples/plugins/todo")
target_compile_definitions(ava_tests PRIVATE AVA_080_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden/ava-080")
target_compile_definitions(ava_tests PRIVATE AVA_M1_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/fixtures/eval/m1")
ava_apply_warnings(ava_tests)
ava_apply_sanitizers(ava_tests)

set(AVA_TEST_SUITES
  core_mode
  session
  core_json_permission
  app_command_classification
  app_command_registry
  tools
  config_context_auth_oauth
  app_compaction
  app_print
  app_event_serialization
  app_event_bus
  app_rpc_queue
  app_rpc_resolver
  app_rpc
  app_runtime
  provider_openai
  provider_anthropic
  provider_gemini
  provider_live_smoke
  agent_loop_resilience
  agent_loop
  agent_tool_dispatcher
  tool_scheduler
  lsp
  plugin
  mcp
  permission_rules
  tui_composer
  run_observer
)
foreach(AVA_TEST_SUITE IN LISTS AVA_TEST_SUITES)
  add_test(NAME ava_tests.${AVA_TEST_SUITE} COMMAND ava_tests ${AVA_TEST_SUITE})
endforeach()
set_tests_properties(ava_tests.provider_live_smoke PROPERTIES SKIP_RETURN_CODE 77)

# The M1 fixture manifest is the source of its focused timeout as well as the
# test-side required-boundary policy. Keep CTest and the scorer pinned to it.
file(READ "${CMAKE_SOURCE_DIR}/fixtures/eval/m1/scoring-manifest.json" AVA_M1_SCORING_MANIFEST)
string(JSON AVA_M1_FOCUSED_TIMEOUT GET "${AVA_M1_SCORING_MANIFEST}" timeouts focused_ctest_seconds)
set_tests_properties(ava_tests.run_observer PROPERTIES TIMEOUT "${AVA_M1_FOCUSED_TIMEOUT}")

add_test(
  NAME ava_cli.headless_rpc_validate_session
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-validate
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_validate.cmake)

add_test(
  NAME ava_cli.headless_rpc_contract
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-contract
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_contract.cmake)

add_test(
  NAME ava_cli.headless_thinking_alias
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-thinking-alias
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_thinking_alias.cmake)

add_test(
  NAME ava_cli.headless_rpc_context_export
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-context-export
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_context_export.cmake)

add_test(
  NAME ava_cli.headless_print_prompt_overrides
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-print-prompt-overrides
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_print_prompt_overrides.cmake)

add_test(
  NAME ava_cli.headless_print_positional_prompt
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-print-positional-prompt
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_print_positional_prompt.cmake)

add_test(
  NAME ava_cli.headless_print_no_session
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-print-no-session
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_print_no_session.cmake)

add_test(
  NAME ava_cli.headless_print_session_startup_options
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-print-session-startup-options
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_print_session_startup_options.cmake)

add_test(
  NAME ava_cli.headless_print_file_args
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-print-file-args
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_print_file_args.cmake)

add_test(
  NAME ava_cli.offline_print_prompt
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-offline-print-prompt
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_offline_print_prompt.cmake)

add_test(
  NAME ava_cli.package_manager_deferred
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_package_manager_deferred.cmake)

add_test(
  NAME ava_cli.headless_mode_json_alias
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-mode-json-alias
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_mode_json_alias.cmake)

add_test(
  NAME ava_cli.headless_tool_visibility
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-tool-visibility
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_tool_visibility.cmake)

add_test(
  NAME ava_cli.headless_rpc_compact_success
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-compact-success
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_compact_success.cmake)

add_test(
  NAME ava_cli.headless_rpc_active_run_rejection
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-active-run-rejection
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_active_run_rejection.cmake)

add_test(
  NAME ava_cli.headless_rpc_permission_reply
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-permission-reply
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_permission_reply.cmake)

add_test(
  NAME ava_cli.headless_rpc_permission_grant
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-permission-grant
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_permission_grant.cmake)

add_test(
  NAME ava_cli.headless_rpc_permission_grant_lifecycle
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-permission-grant-lifecycle
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_permission_grant_lifecycle.cmake)

add_test(
  NAME ava_cli.headless_rpc_question_reply
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-question-reply
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_question_reply.cmake)

add_test(
  NAME ava_cli.headless_rpc_question_reply_multi
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-question-reply-multi
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_question_reply_multi.cmake)

add_test(
  NAME ava_cli.headless_rpc_provider_error
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-provider-error
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_provider_error.cmake)

add_test(
  NAME ava_cli.headless_rpc_tool_failure
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-tool-failure
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_tool_failure.cmake)

add_test(
  NAME ava_cli.headless_performance_smoke
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-performance-smoke
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_performance_smoke.cmake)

add_test(
  NAME ava_cli.headless_e2e_model_smoke
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-e2e-model-smoke
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_e2e_model_smoke.cmake)
set_tests_properties(ava_cli.headless_e2e_model_smoke PROPERTIES TIMEOUT 60)

add_test(
  NAME ava_cli.headless_rpc_bash_process_cleanup
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-bash-process-cleanup
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_bash_process_cleanup.cmake)

add_test(
  NAME ava_cli.headless_rpc_cancel
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-cancel
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_cancel.cmake)

add_test(
  NAME ava_cli.headless_rpc_error_recovery
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-error-recovery
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_error_recovery.cmake)

add_test(
  NAME ava_cli.headless_rpc_mcp_commands
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_MCP_SERVER_EXE=$<TARGET_FILE:ava_fake_mcp_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-mcp-commands
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_mcp_commands.cmake)

add_test(
  NAME ava_cli.headless_rpc_command_registry
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_FAKE_PROVIDER_EXE=$<TARGET_FILE:ava_fake_provider_server>
          -DAVA_FAKE_MCP_SERVER_EXE=$<TARGET_FILE:ava_fake_mcp_server>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-command-registry
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_command_registry.cmake)

add_test(
  NAME ava_cli.headless_rpc_plugin_commands
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-plugin-commands
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_plugin_commands.cmake)

add_test(
  NAME ava_cli.headless_rpc_sample_plugin
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-sample-plugin
          -DAVA_SAMPLE_TODO_PLUGIN_DIR=${CMAKE_SOURCE_DIR}/examples/plugins/todo
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_sample_plugin.cmake)

add_test(
  NAME ava_cli.headless_rpc_mcp_config_errors
  COMMAND ${CMAKE_COMMAND}
          -DAVA_EXE=$<TARGET_FILE:ava>
          -DAVA_CLI_TEST_ROOT=${CMAKE_BINARY_DIR}/cli-headless-rpc-mcp-config-errors
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_headless_rpc_mcp_config_errors.cmake)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(
    NAME ava_tui.tmux_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tui_tmux_smoke.py
            --ava $<TARGET_FILE:ava>
            --root ${CMAKE_BINARY_DIR}/tui-tmux-smoke)
  set_tests_properties(ava_tui.tmux_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 45)

  add_test(
    NAME ava_tui.kitty_image_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tui_kitty_image_smoke.py
            --ava $<TARGET_FILE:ava>
            --root ${CMAKE_BINARY_DIR}/tui-kitty-image-smoke)
  set_tests_properties(ava_tui.kitty_image_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 20)

  add_test(
    NAME ava_tui.osc8_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tui_osc8_smoke.py
            --ava $<TARGET_FILE:ava>
            --fake-provider $<TARGET_FILE:ava_fake_provider_server>
            --root ${CMAKE_BINARY_DIR}/tui-osc8-smoke)
  set_tests_properties(ava_tui.osc8_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 20)
endif()
