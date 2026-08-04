option(AVA_REQUIRE_ACP_SDK_INTEROP "Require installed official ACP SDK interoperability dependencies" OFF)
option(AVA_ENABLE_ACPX_INTEROP "Enable the opt-in acpx interoperability smoke" OFF)
find_program(AVA_NODE_EXECUTABLE NAMES node DOC "Node.js executable used only by ACP interoperability tests")

if(AVA_REQUIRE_ACP_SDK_INTEROP AND NOT AVA_NODE_EXECUTABLE)
  message(FATAL_ERROR "AVA_REQUIRE_ACP_SDK_INTEROP requires Node.js and installed tests/acp-sdk packages")
endif()

set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)

add_library(ava_test_support
  "support/acp_test_support.cpp"
  "support/agent_loop_test_support.cpp"
  "support/app_rpc_test_support.cpp"
  "support/app_runtime_support.cpp"
  "support/fake_transport.cpp"
  "support/session_test_support.cpp"
  "support/test_harness.cpp"
)
target_link_libraries(ava_test_support PUBLIC ${AVA_BACKEND_LIBRARIES} ${AICXX_OBJECTS_LIST})
target_include_directories(ava_test_support PUBLIC ${CMAKE_SOURCE_DIR})
ava_apply_warnings(ava_test_support)
ava_apply_sanitizers(ava_test_support)

add_library(ava_tui_test_support "support/tui_test_support.cpp")
target_link_libraries(ava_tui_test_support PRIVATE ava_test_support)
target_include_directories(ava_tui_test_support PUBLIC ${CMAKE_SOURCE_DIR})
ava_apply_warnings(ava_tui_test_support)
ava_apply_sanitizers(ava_tui_test_support)

add_executable(ava_fake_lsp_server "support/fake_lsp_server.cpp")
target_link_libraries(ava_fake_lsp_server PRIVATE ${AVA_BACKEND_LIBRARIES})
ava_apply_warnings(ava_fake_lsp_server)
ava_apply_sanitizers(ava_fake_lsp_server)

add_executable(ava_lsp_real_clangd_smoke "lsp_real_clangd_smoke.cpp")
target_link_libraries(ava_lsp_real_clangd_smoke PRIVATE ${AVA_BACKEND_LIBRARIES})
ava_apply_warnings(ava_lsp_real_clangd_smoke)
ava_apply_sanitizers(ava_lsp_real_clangd_smoke)

add_executable(ava_fake_mcp_server "support/fake_mcp_server.cpp")
target_link_libraries(ava_fake_mcp_server PRIVATE ${AVA_BACKEND_LIBRARIES})
ava_apply_warnings(ava_fake_mcp_server)
ava_apply_sanitizers(ava_fake_mcp_server)

add_executable(ava_fake_provider_server "support/fake_provider_server.cpp")
ava_apply_warnings(ava_fake_provider_server)
ava_apply_sanitizers(ava_fake_provider_server)

add_executable(ava_rpc_broken_pipe_subprocess_test "rpc_broken_pipe_subprocess_test.cpp")
add_dependencies(ava_rpc_broken_pipe_subprocess_test ava)
target_compile_definitions(ava_rpc_broken_pipe_subprocess_test PRIVATE AVA_EXE_PATH="$<TARGET_FILE:ava>")
ava_apply_warnings(ava_rpc_broken_pipe_subprocess_test)
ava_apply_sanitizers(ava_rpc_broken_pipe_subprocess_test)

set(AVA_TESTS_SOURCES
  "acp_client_tools_tests.cpp"
  "acp_content_tests.cpp"
  "acp_peer_delivery_tests.cpp"
  "acp_peer_lifecycle_tests.cpp"
  "acp_permission_gateway_tests.cpp"
  "acp_service_tests.cpp"
  "acp_tests.cpp"
  "agent_loop_background_job_tests.cpp"
  "agent_loop_background_subagent_tests.cpp"
  "agent_loop_command_safety_tests.cpp"
  "agent_loop_core_tests.cpp"
  "agent_loop_history_tests.cpp"
  "agent_loop_interaction_tests.cpp"
  "agent_loop_resilience_tests.cpp"
  "agent_loop_subagent_tests.cpp"
  "agent_loop_tests.cpp"
  "agent_loop_tool_execution_tests.cpp"
  "agent_tool_dispatcher_tests.cpp"
  "agent_todo_tests.cpp"
  "app_compaction_tests.cpp"
  "branch_summary_coordinator_tests.cpp"
  "app_command_registry_tests.cpp"
  "app_print_tests.cpp"
  "app_rpc_queue_tests.cpp"
  "app_rpc_resolver_tests.cpp"
  "app_rpc_tests.cpp"
  "app_rpc_commands_tests.cpp"
  "app_rpc_direct_tests.cpp"
  "app_rpc_line_reader_tests.cpp"
  "app_rpc_prompt_tests.cpp"
  "app_rpc_publication_tests.cpp"
  "app_rpc_serialization_tests.cpp"
  "app_rpc_session_tests.cpp"
  "app_runtime_tests.cpp"
  "app_command_classification_tests.cpp"
  "app_event_serialization_tests.cpp"
  "app_runtime_startup_tests.cpp"
  "app_runtime_plugin_tests.cpp"
  "app_runtime_prompt_tests.cpp"
  "app_runtime_command_tests.cpp"
  "app_runtime_command_ui_tests.cpp"
  "app_runtime_command_catalog_tests.cpp"
  "app_runtime_command_auth_tests.cpp"
  "app_runtime_command_tool_tests.cpp"
  "app_runtime_command_session_tests.cpp"
  "app_runtime_session_command_tests.cpp"
  "app_runtime_catalog_tests.cpp"
  "app_runtime_model_tests.cpp"
  "config_context_auth_oauth_tests.cpp"
  "provider_config_tests.cpp"
  "provider_user_catalog_tests.cpp"
  "provider_builtin_generic_tests.cpp"
  "command_tests.cpp"
  "core_tests.cpp"
  "core_mode_json_permissions_tests.cpp"
  "diagnostics_tests.cpp"
  "events_tests.cpp"
  "lsp_tests.cpp"
  "mcp_tests.cpp"
  "permission_rules_tests.cpp"
  "plugin_tests.cpp"
  "plugin_ui_tests.cpp"
  "provider_anthropic_tests.cpp"
  "provider_gemini_tests.cpp"
  "provider_live_smoke_tests.cpp"
  "provider_openai_compatible_tests.cpp"
  "provider_openai_contract_http_tests.cpp"
  "provider_openai_contract_request_tests.cpp"
  "provider_openai_contract_sse_tests.cpp"
  "provider_openai_contract_terminal_tests.cpp"
  "provider_openai_limits_tests.cpp"
  "provider_openai_output_tests.cpp"
  "provider_openai_tests.cpp"
  "session_tests.cpp"
  "session_assistant_output_tests.cpp"
  "session_attachment_tests.cpp"
  "session_authority_tests.cpp"
  "session_compaction_tests.cpp"
  "session_projection_tests.cpp"
  "session_recovery_tests.cpp"
  "session_stats_tests.cpp"
  "session_store_tests.cpp"
  "session_tree_tests.cpp"
  "session_run_controller_tests.cpp"
  "session_title_coordinator_tests.cpp"
  "subagent_coordinator_tests.cpp"
  "subagent_delivery_manager_tests.cpp"
  "test_harness_tests.cpp"
  "tool_scheduler_tests.cpp"
  "tools_file_tests.cpp"
  "tools_process_network_tests.cpp"
  "tools_search_tests.cpp"
  "tools_tests.cpp"
  "tui_composer_tests.cpp"
  "tui_completion_tests.cpp"
  "tui_composer_rendering_tests.cpp"
  "tui_keybinding_tests.cpp"
  "tui_markdown_tests.cpp"
  "tui_modal_tests.cpp"
  "tui_permission_tests.cpp"
  "tui_runtime_state_tests.cpp"
  "tui_selector_tests.cpp"
  "tui_terminal_input_tests.cpp"
  "tui_tool_card_tests.cpp"
  "tui_transcript_tests.cpp"
  "tui_transcript_selection_tests.cpp"
  "tui_transcript_search_tests.cpp"
  "run_observer_tests.cpp"
  "runtime_diagnostics_tests.cpp"
  "containment_tests.cpp"
)
# The debug suite relies on libcwd-backed machinery (ava_debug, CWDEBUG), so it is
# only compiled in when libcwd is effectively enabled.
if(OptionEnableLibcwd)
  list(APPEND AVA_TESTS_SOURCES "debug_tests.cpp")
endif()

add_executable(ava_tests ${AVA_TESTS_SOURCES})
target_link_libraries(ava_tests PRIVATE ${AVA_FRONTEND_LIBRARIES} ${AVA_BACKEND_LIBRARIES} ava_test_support ava_tui_test_support ${AICXX_OBJECTS_LIST})
target_include_directories(ava_tests PRIVATE ${CURSES_INCLUDE_DIRS})
add_dependencies(ava_tests ava_fake_lsp_server ava_fake_mcp_server ava_fake_provider_server)
target_compile_definitions(ava_tests PRIVATE AVA_FAKE_LSP_SERVER_PATH="$<TARGET_FILE:ava_fake_lsp_server>")
target_compile_definitions(ava_tests PRIVATE AVA_FAKE_MCP_SERVER_PATH="$<TARGET_FILE:ava_fake_mcp_server>")
target_compile_definitions(ava_tests PRIVATE AVA_SAMPLE_TODO_PLUGIN_DIR="${CMAKE_SOURCE_DIR}/examples/plugins/todo")
target_compile_definitions(ava_tests PRIVATE AVA_080_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden/ava-080")
target_compile_definitions(ava_tests PRIVATE AVA_M1_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/fixtures/eval/m1")
target_compile_definitions(ava_tests PRIVATE AVA_RPC_V1_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden/rpc-v1")
target_compile_definitions(ava_tests PRIVATE AVA_RUNTIME_EVENTS_V1_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden/runtime-events-v1")
target_compile_definitions(ava_tests PRIVATE AVA_ACP_V1_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures/acp-v1")
target_compile_definitions(ava_tests PRIVATE AVA_TESTS_BINARY_DIR="${CMAKE_BINARY_DIR}")
ava_apply_warnings(ava_tests)
ava_apply_sanitizers(ava_tests)

set(AVA_TEST_SUITES
  core_mode
  test_harness
  diagnostics
  acp
  session
  session_run_controller
  session_title_coordinator
  branch_summary_coordinator
  subagent_coordinator
  subagent_delivery_manager
  core_json_permission
  app_command_classification
  app_command_registry
  tools
  config_context_auth_oauth
  provider_config
  provider_user_catalog
  provider_builtin_generic
  command
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
  agent_todo
  tool_scheduler
  lsp
  plugin
  plugin_ui
  mcp
  permission_rules
  tui_composer
  run_observer
  runtime_diagnostics
  containment
)

foreach(AVA_TEST_SUITE IN LISTS AVA_TEST_SUITES)
  add_test(NAME ava_tests.${AVA_TEST_SUITE} COMMAND ava_tests ${AVA_TEST_SUITE})
  # Keep pre-main libcwd diagnostics quiet without replacing the caller's
  # rcfile policy. ava_tests itself routes all output produced from main.
  # AVA_NO_DEBUG_OUTPUT makes ava::app::debug_init() skip NAMESPACE_DEBUG::init()
  # when no private output was requested, so libcwd cannot contaminate the test
  # protocol. A nonempty inherited AVA_DEBUG_OUTPUT_DIR overrides suppression
  # after LibcwdOutputSink installs its validated per-suite private stream.
  set_tests_properties(ava_tests.${AVA_TEST_SUITE} PROPERTIES
    ENVIRONMENT "LIBCWD_NO_STARTUP_MSGS=1;AVA_NO_DEBUG_OUTPUT=1")
endforeach()
set_tests_properties(ava_tests.provider_live_smoke PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(ava_tests.containment PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(ava_tests.tool_scheduler PROPERTIES TIMEOUT 30)

add_test(NAME ava_tests.lsp_real_clangd_smoke COMMAND ava_lsp_real_clangd_smoke)
set_tests_properties(ava_tests.lsp_real_clangd_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 20 LABELS "lsp;offline;optional")

add_test(NAME ava_cli.rpc_broken_pipe_subprocess COMMAND ava_rpc_broken_pipe_subprocess_test)
set_tests_properties(ava_cli.rpc_broken_pipe_subprocess PROPERTIES TIMEOUT 10)

if(AVA_NODE_EXECUTABLE)
  set(AVA_ACP_SDK_INTEROP_COMMAND
    ${AVA_NODE_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/acp-sdk/interop.mjs
    --ava $<TARGET_FILE:ava>
    --fake-provider $<TARGET_FILE:ava_fake_provider_server>)
  if(AVA_REQUIRE_ACP_SDK_INTEROP)
    list(APPEND AVA_ACP_SDK_INTEROP_COMMAND --required)
  endif()
  add_test(NAME ava_cli.acp_sdk_interop COMMAND ${AVA_ACP_SDK_INTEROP_COMMAND})
else()
  find_program(AVA_SH_EXECUTABLE NAMES sh REQUIRED)
  add_test(NAME ava_cli.acp_sdk_interop COMMAND ${AVA_SH_EXECUTABLE} -c "exit 77")
endif()
set_tests_properties(
  ava_cli.acp_sdk_interop
  PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 75 LABELS "acp;interop;official-sdk")

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
set_tests_properties(ava_cli.headless_rpc_bash_process_cleanup PROPERTIES TIMEOUT 75)

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

add_test(
  NAME ava_build.ctags_json_detection
  COMMAND ${CMAKE_COMMAND}
          -DAVA_SOURCE_DIR=${CMAKE_SOURCE_DIR}
          -DAVA_TEST_ROOT=${CMAKE_BINARY_DIR}/ctags-json-detection
          -P ${CMAKE_CURRENT_SOURCE_DIR}/ctags_json_detection_test.cmake)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(AVA_ENABLE_ACPX_INTEROP AND NOT Python3_Interpreter_FOUND)
  message(FATAL_ERROR "AVA_ENABLE_ACPX_INTEROP requires Python 3 for the bounded launcher")
endif()
if(Python3_Interpreter_FOUND)
  add_test(
    NAME ava_tests.markdown_link_verifier
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/verify_markdown_links_test.py
            --script ${CMAKE_SOURCE_DIR}/scripts/verify-markdown-links.py)
  set_tests_properties(ava_tests.markdown_link_verifier PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tests.markdown_links_source
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/verify-markdown-links.py
            ${CMAKE_SOURCE_DIR}
            --source-tree)
  set_tests_properties(ava_tests.markdown_links_source PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tests.documentation_structure_checker
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/verify_documentation_structure_test.py
            --script ${CMAKE_SOURCE_DIR}/scripts/verify-documentation-structure.py)
  set_tests_properties(ava_tests.documentation_structure_checker PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tests.documentation_structure_source
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/verify-documentation-structure.py
            ${CMAKE_SOURCE_DIR})
  set_tests_properties(ava_tests.documentation_structure_source PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tests.module_dependency_rules
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/module_dependency_rules_test.py
            --source ${CMAKE_SOURCE_DIR}
            --policy ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/module_dependency_rules.json)
  set_tests_properties(ava_tests.module_dependency_rules PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tests.module_dependency_rules_self_test
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/module_dependency_rules_test.py
            --self-test)
  set_tests_properties(ava_tests.module_dependency_rules_self_test PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_cli.doctor_support
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/doctor_support_cli_test.py
            --ava $<TARGET_FILE:ava>
            --root ${CMAKE_BINARY_DIR}/doctor-support-cli)
  set_tests_properties(ava_cli.doctor_support PROPERTIES TIMEOUT 30 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_cli.runtime_diagnostics
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/runtime_diagnostics_cli_test.py
            --ava $<TARGET_FILE:ava>
            --root ${CMAKE_BINARY_DIR}/runtime-diagnostics-cli)
  set_tests_properties(ava_cli.runtime_diagnostics PROPERTIES TIMEOUT 30 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_build.parallel_test_runner
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/parallel_test_runner_test.py
            --script ${CMAKE_SOURCE_DIR}/scripts/run-tests.sh
            --build-script ${CMAKE_SOURCE_DIR}/scripts/build.sh
            --root ${CMAKE_BINARY_DIR}/parallel-test-runner)
  set_tests_properties(ava_build.parallel_test_runner PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tests.session_read_authority_inventory
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/session_read_authority_inventory_test.py
            --source ${CMAKE_SOURCE_DIR})
  set_tests_properties(ava_tests.session_read_authority_inventory PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tests.session_reference_inventory
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/session_reference_inventory_test.py
            --source ${CMAKE_SOURCE_DIR})
  set_tests_properties(ava_tests.session_reference_inventory PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tests.plugin_ui_authority_inventory
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/plugin_ui_authority_inventory_test.py
            --source ${CMAKE_SOURCE_DIR})
  set_tests_properties(ava_tests.plugin_ui_authority_inventory PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tests.acp_interop_manifest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/acp_interop_manifest_test.py
            --source ${CMAKE_SOURCE_DIR})
  set_tests_properties(ava_tests.acp_interop_manifest PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1" LABELS "acp;interop;offline")

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(
      NAME ava_tests.live_acp_dogfood_launcher
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/live_acp_dogfood_test.py
              --source ${CMAKE_SOURCE_DIR})
    set_tests_properties(ava_tests.live_acp_dogfood_launcher PROPERTIES TIMEOUT 30 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1" LABELS "acp;interop;offline")
  endif()

  if(AVA_ENABLE_ACPX_INTEROP)
    if(NOT AVA_NODE_EXECUTABLE)
      message(FATAL_ERROR "AVA_ENABLE_ACPX_INTEROP requires exact Node.js 24.13.1")
    endif()
    add_test(
      NAME ava_cli.acpx_interop
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/acp-acpx/interop.py
              --ava $<TARGET_FILE:ava>
              --fake-provider $<TARGET_FILE:ava_fake_provider_server>
              --node ${AVA_NODE_EXECUTABLE}
              --acpx ${CMAKE_CURRENT_SOURCE_DIR}/acp-acpx/node_modules/.bin/acpx
              --root ${CMAKE_BINARY_DIR}/acpx-interop)
    set_tests_properties(ava_cli.acpx_interop PROPERTIES TIMEOUT 40 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1" LABELS "acp;interop;acpx;opt-in")
  endif()

  add_test(
    NAME ava_tests.acp_support_manifest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/acp_support_manifest_test.py
            --source ${CMAKE_SOURCE_DIR})
  set_tests_properties(ava_tests.acp_support_manifest PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_cli.acp_subprocess
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/acp_subprocess_test.py
            --ava $<TARGET_FILE:ava>
            --fake-provider $<TARGET_FILE:ava_fake_provider_server>
            --fake-mcp $<TARGET_FILE:ava_fake_mcp_server>
            --root ${CMAKE_BINARY_DIR}/acp-subprocess)
  set_tests_properties(ava_cli.acp_subprocess PROPERTIES TIMEOUT 60 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_cli.rpc_parser_subprocess
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/rpc_parser_subprocess_test.py
            --ava $<TARGET_FILE:ava>
            --root ${CMAKE_BINARY_DIR}/rpc-parser-subprocess)
  set_tests_properties(ava_cli.rpc_parser_subprocess PROPERTIES TIMEOUT 20 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tests.rpc_contract_manifest
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/rpc_contract_manifest_test.py
            --source ${CMAKE_SOURCE_DIR}
            --ava $<TARGET_FILE:ava>)
  set_tests_properties(ava_tests.rpc_contract_manifest PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tests.rpc_python_client
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/rpc_client_tests.py
            --source ${CMAKE_SOURCE_DIR})
  set_tests_properties(ava_tests.rpc_python_client PROPERTIES TIMEOUT 15 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_cli.rpc_python_client_e2e
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/rpc_client_e2e.py
            --ava $<TARGET_FILE:ava>
            --fake-provider $<TARGET_FILE:ava_fake_provider_server>
            --source ${CMAKE_SOURCE_DIR}
            --root ${CMAKE_BINARY_DIR}/rpc-python-client-e2e)
  set_tests_properties(ava_cli.rpc_python_client_e2e PROPERTIES TIMEOUT 45 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_release.provenance
    COMMAND ${CMAKE_COMMAND} -E env
            PYTHONDONTWRITEBYTECODE=1
            AVA_RELEASE_TEST_BINARY=$<TARGET_FILE:ava>
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/release_provenance_tests.py)
  set_tests_properties(ava_release.provenance PROPERTIES TIMEOUT 15)

  add_test(
    NAME ava_release.install_component
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/install_component_tests.py
            --cmake ${CMAKE_COMMAND}
            --source ${CMAKE_SOURCE_DIR}
            --build ${CMAKE_BINARY_DIR})
  set_tests_properties(ava_release.install_component PROPERTIES TIMEOUT 75 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT OptionEnableLibcwd)
    add_test(
      NAME ava_release.package_linux
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/package_linux_tests.py
              --script ${CMAKE_SOURCE_DIR}/scripts/package-linux.sh
              --ava $<TARGET_FILE:ava>
              --fake-provider $<TARGET_FILE:ava_fake_provider_server>
              --repo ${CMAKE_SOURCE_DIR}
              --build-dir ${CMAKE_BINARY_DIR}
              --root ${CMAKE_BINARY_DIR}/package-linux-tests)
    set_tests_properties(ava_release.package_linux PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 240 ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
  endif()

  set(AVA_TUI_TMUX_SMOKE_SCENARIOS
    suspend_resume
    keybind_conflict
    theme_env
    theme_persisted
    nested_settings_preview
    startup_overview
    setup_wizard
    active_run
    restore_followup
    streaming_scroll
    transcript_search
    transcript_selection
    subagent_workspace
    branch_summary
    main_startup_trust_keybinds
    main_models_selectors
    main_editor_input
    main_slash_completions
    main_permission_flow
    main_question_flow
    main_session_mgmt
    main_paste_scrollback_attach
    plugin_ui)
  foreach(AVA_TUI_TMUX_SMOKE_SCENARIO IN LISTS AVA_TUI_TMUX_SMOKE_SCENARIOS)
    add_test(
      NAME ava_tui.tmux_smoke_${AVA_TUI_TMUX_SMOKE_SCENARIO}
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/tui_tmux_smoke.py
              --scenario ${AVA_TUI_TMUX_SMOKE_SCENARIO}
              --ava $<TARGET_FILE:ava>
              --fake-provider $<TARGET_FILE:ava_fake_provider_server>
              --root ${CMAKE_BINARY_DIR}/tui-tmux-smoke/${AVA_TUI_TMUX_SMOKE_SCENARIO})
    set_tests_properties(
      ava_tui.tmux_smoke_${AVA_TUI_TMUX_SMOKE_SCENARIO}
      PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 60
        TIMEOUT_SIGNAL_NAME SIGTERM
        TIMEOUT_SIGNAL_GRACE_PERIOD 10
        LABELS "tui;tmux;smoke;opt-in"
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
  endforeach()

  add_test(
    NAME ava_tui.kitty_image_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tui_kitty_image_smoke.py
            --protocol kitty
            --ava $<TARGET_FILE:ava>
            --root ${CMAKE_BINARY_DIR}/tui-kitty-image-smoke)
  set_tests_properties(
    ava_tui.kitty_image_smoke
    PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 35
      LABELS "tui;pty;graphics;kitty;smoke;opt-in"
      ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tui.iterm2_image_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tui_kitty_image_smoke.py
            --protocol iterm2
            --ava $<TARGET_FILE:ava>
            --root ${CMAKE_BINARY_DIR}/tui-iterm2-image-smoke)
  set_tests_properties(
    ava_tui.iterm2_image_smoke
    PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 35
      LABELS "tui;pty;graphics;iterm2;smoke;opt-in"
      ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tui.terminal_lifecycle_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tui_terminal_lifecycle_smoke.py
            --ava $<TARGET_FILE:ava>
            --root ${CMAKE_BINARY_DIR}/tui-terminal-lifecycle-smoke)
  set_tests_properties(
    ava_tui.terminal_lifecycle_smoke
    PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 75
      LABELS "tui;pty;terminal-lifecycle;smoke;opt-in"
      ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  add_test(
    NAME ava_tui.osc8_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tui_osc8_smoke.py
            --ava $<TARGET_FILE:ava>
            --fake-provider $<TARGET_FILE:ava_fake_provider_server>
            --root ${CMAKE_BINARY_DIR}/tui-osc8-smoke)
  set_tests_properties(
    ava_tui.osc8_smoke
    PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 60
      LABELS "tui;pty;osc8;osc52;smoke;opt-in"
      ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

  if(OptionEnableLibcwd)
    # The debug-printer coverage check requires the `ctags-json` target and the generated tags.json,
    # both of which only exist when libcwd debug support is enabled (OptionEnableLibcwd).
    # It rebuilds ctags-json to guarantee a fresh tags.json, then asserts that no src/ava/ header
    # type is missing a print_members opt-in/opt-out marker, printing any offenders on failure.
    add_test(
      NAME ava_debug.print_members_coverage
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/print_members_coverage_test.py
              --source-dir ${CMAKE_SOURCE_DIR}
              --build-dir ${CMAKE_BINARY_DIR})
    set_tests_properties(ava_debug.print_members_coverage PROPERTIES TIMEOUT 60)
  endif()
endif()

if(OptionEnableLibcwd)
  # The print_members_format suite relies on libcwd-backed machinery (ava_debug, CWDEBUG),
  # so it is only registered when libcwd is effectively enabled.
  add_test(
    NAME ava_debug.print_members_format
    COMMAND ava_tests debug)
  set_tests_properties(ava_debug.print_members_format PROPERTIES
    ENVIRONMENT "LIBCWD_NO_STARTUP_MSGS=1;AVA_NO_DEBUG_OUTPUT=1")

  if(Python3_Interpreter_FOUND)
    add_test(
      NAME ava_debug.libcwd_output_routing
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/libcwd_debug_output_test.py
              --ava $<TARGET_FILE:ava_tests>
              --ava-exe $<TARGET_FILE:ava>)
    set_tests_properties(ava_debug.libcwd_output_routing PROPERTIES
      TIMEOUT 120
      TIMEOUT_SIGNAL_NAME SIGTERM
      TIMEOUT_SIGNAL_GRACE_PERIOD 10
      ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1;LIBCWD_NO_STARTUP_MSGS=1")
  endif()
endif()

# Existing integration providers use exact finite response scripts. Automatic
# title generation has dedicated fake-transport coverage and must not consume
# those scripts or make any external provider call during ordinary CTest runs.
get_property(AVA_REGISTERED_TESTS DIRECTORY PROPERTY TESTS)
if(AVA_REGISTERED_TESTS)
  set_property(TEST ${AVA_REGISTERED_TESTS} APPEND PROPERTY ENVIRONMENT "AVA_SESSION_TITLES=off")
  foreach(_ava_test IN LISTS AVA_REGISTERED_TESTS)
    # One canonical identity tells any child `ava` process both that it is
    # running under CTest and which per-test libcwd filename it must use.
    set_property(TEST ${_ava_test} APPEND PROPERTY ENVIRONMENT "AVA_TEST_NAME=${_ava_test}")
  endforeach()
endif()

# ava_cli.* and ava_tui.* drive the real `ava` binary (cmake -P shell drivers
# or python harnesses). Give them the same libcwd-suppression pair ava_tests.*
# gets above so ava::app::debug_init() skips NAMESPACE_DEBUG::init() and cannot
# emit on the stdout/stderr those tests parse. The pair is inherited by the
# cmake driver processes (and thus their `ava` child) and by python harnesses
# that start from os.environ.copy(). ava_debug.libcwd_output_routing is
# deliberately excluded: it runs libcwd_debug_output_test.py, which exercises
# debug-output routing and must not suppress it.
set(AVA_AVA_LAUNCHING_TESTS "")
foreach(_ava_test IN LISTS AVA_REGISTERED_TESTS)
  if(_ava_test MATCHES "^(ava_cli\\.|ava_tui\\.)")
    list(APPEND AVA_AVA_LAUNCHING_TESTS "${_ava_test}")
  endif()
endforeach()
if(AVA_AVA_LAUNCHING_TESTS)
  set_property(TEST ${AVA_AVA_LAUNCHING_TESTS} APPEND PROPERTY ENVIRONMENT "LIBCWD_NO_STARTUP_MSGS=1;AVA_NO_DEBUG_OUTPUT=1")
endif()

# --- Debug knob: disable CTest per-test timeouts -------------------------
# Setting AVA_DEBUG_NO_TIMEOUT in the environment at *configure* time
# (e.g. `AVA_DEBUG_NO_TIMEOUT=1 aap-configure`) raises every CTest per-test
# TIMEOUT in this directory so ctest no longer SIGTERMs a hung or
# step-debugged test while a debugger is attached. Optional
# AVA_DEBUG_NO_TIMEOUT_SECONDS overrides the one-hour default, which is the
# literal floor requested by the knob ("at least one hour, or effectively
# never"); set it to a very large value to approximate "never time out".
#
# This is configure-time only because CTest bakes TIMEOUT into
# CTestTestfile.cmake at generate time; toggle the variable and re-run
# aap-configure (aap-test alone cannot change it).
#
# Only the ctest kill-timer is touched. In-test deadlines (condition_variable
# waits, execute_process TIMEOUTs, futures) are intentionally left
# as-authored: many of them encode negative assertions ("this must not happen")
# and inflating them blindly would turn green tests into multi-hour hangs.
if(DEFINED ENV{AVA_DEBUG_NO_TIMEOUT})
  set(AVA_DEBUG_NO_TIMEOUT_SECONDS "$ENV{AVA_DEBUG_NO_TIMEOUT_SECONDS}")
  if(AVA_DEBUG_NO_TIMEOUT_SECONDS STREQUAL "" OR NOT AVA_DEBUG_NO_TIMEOUT_SECONDS MATCHES "^[1-9][0-9]*$")
    set(AVA_DEBUG_NO_TIMEOUT_SECONDS 3600)
  endif()
  if(AVA_REGISTERED_TESTS)
    list(LENGTH AVA_REGISTERED_TESTS AVA_DEBUG_NO_TIMEOUT_COUNT)
    set_property(TEST ${AVA_REGISTERED_TESTS} PROPERTY TIMEOUT ${AVA_DEBUG_NO_TIMEOUT_SECONDS})
    message(STATUS
      "${Red}AVA_DEBUG_NO_TIMEOUT: raised CTest TIMEOUT to ${AVA_DEBUG_NO_TIMEOUT_SECONDS}s for "
      "${AVA_DEBUG_NO_TIMEOUT_COUNT} test(s); rerun configure without it to restore normal timeouts.${ColourReset}")
  endif()
endif()
