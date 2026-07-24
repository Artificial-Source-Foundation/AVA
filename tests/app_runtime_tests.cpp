#include "sys.h"
#include "tests/app_runtime_test_declarations.h"

using namespace ava::tests::app_runtime_tests;

void run_app_command_classification_tests()
{
  test_command_classification();
  test_repository_build_test_headless_decision_matrix();
}

void run_app_event_serialization_tests()
{
  test_app_event_serialization();
}

void run_app_runtime_tests()
{
  test_app_runtime_open_session_and_context_prompt();
  test_app_runtime_no_session_mode();
  test_app_runtime_lifecycle_adapters_preserve_strict_open_policy();
  test_app_runtime_session_startup_options();
  test_app_runtime_recovers_torn_tail_before_resume_and_startup_fork();
  test_app_runtime_reconciles_committed_function_calls_on_resume();
  test_app_runtime_cli_prompt_overrides();
  test_app_runtime_project_trust_malformed_diagnostics();
  test_app_runtime_enabled_plugin_resources_autoload();
  test_app_runtime_project_plugin_resources_follow_trust_gate();
  test_app_runtime_enabled_plugin_resource_failures_are_context_visible();
  test_app_runtime_plugin_install_remove_commands();
  test_app_context_reports_lsp_config_load_errors();
  test_app_run_prompt_emits_events();
  test_app_run_prompt_expands_file_references();
  test_app_run_prompt_sends_imported_image_attachment();
  test_app_clipboard_image_file_override_imports_attachment();
  test_app_run_prompt_emits_provider_retry_events_when_enabled();
  test_app_run_prompt_observation_shares_context_across_compaction_and_retry();
  test_app_run_prompt_emits_tool_progress_and_session_spill();
  test_app_first_run_auth_onboarding();
  test_app_run_prompt_event_sink_failure_cancels_before_next_provider_call();
  test_app_command_dispatcher();
  test_app_session_jsonl_import_export_portable_attachments();
  test_app_session_jsonl_export_sanitizes_private_reasoning_replay_metadata();
  test_app_session_branch_commands();
  test_app_session_new_resume_commands();
  test_app_sessionless_new_and_resume_commands();
  test_app_sessionless_new_preserves_supplied_anchor();
  test_app_session_metadata_commands();
  test_application_catalog_cache_reuses_workspace_and_session_indexes();
  test_application_catalog_coordinator_serializes_refresh_and_snapshot();
  test_application_catalog_current_session_incremental_refresh();
  test_request_projection_validates_committed_v4_history();
  test_runtime_model_switch_accepts_committed_openai_responses_reasoning();
  test_app_runtime_model_switch_persists_and_reopens();
  test_app_runtime_model_switch_projects_incompatible_history_at_request_time();
  test_app_runtime_reasoning_selection_persists_and_requests();
  test_app_runtime_branch_construction_failure_rolls_back_created_file();
  test_app_runtime_initial_reasoning_level_option();
}
