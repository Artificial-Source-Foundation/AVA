#pragma once

#include "ava/app/command_catalog.h"
#include "ava/app/runtime/Session.h"
#include "ava/config/xdg_paths.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ava::tests::app_runtime_tests {

void test_command_classification();
void test_repository_build_test_headless_decision_matrix();
void test_app_event_serialization();
void test_extension_resource_policy_derives_synthetic_paths_and_trust();
void test_app_runtime_open_session_and_context_prompt();
void test_app_runtime_preserves_legacy_subagent_job_tree();
void test_app_active_context_status_tracks_compaction_projection();
void test_app_runtime_no_session_mode();
void test_app_runtime_session_startup_options();
void test_app_runtime_recovers_torn_tail_before_resume_and_startup_fork();
void test_app_runtime_reconciles_committed_function_calls_on_resume();
void test_app_runtime_cli_prompt_overrides();
void test_app_runtime_project_trust_malformed_diagnostics();
void test_app_runtime_enabled_plugin_resources_autoload();
void test_app_runtime_project_plugin_resources_follow_trust_gate();
void test_app_runtime_enabled_plugin_resource_failures_are_context_visible();
void test_app_runtime_plugin_install_remove_commands();
void test_app_context_reports_lsp_config_load_errors();
void test_app_run_prompt_isolates_ambient_extensions();
void test_app_run_prompt_emits_events();
void test_app_run_prompt_expands_file_references();
void test_app_run_prompt_sends_imported_image_attachment();
void test_app_clipboard_image_file_override_imports_attachment();
void test_app_run_prompt_emits_provider_retry_events_when_enabled();
void test_app_run_prompt_observation_shares_context_across_compaction_and_retry();
void test_app_run_prompt_emits_tool_progress_and_session_spill();
void test_app_first_run_auth_onboarding();
void test_app_run_prompt_event_sink_failure_cancels_before_next_provider_call();
void test_app_command_dispatcher();
void test_app_session_jsonl_import_export_portable_attachments();
void test_app_session_jsonl_export_sanitizes_private_reasoning_replay_metadata();
void test_app_session_branch_commands();
void test_app_session_new_resume_commands();
void test_app_session_metadata_commands();
void test_request_projection_validates_committed_v4_history();
void test_runtime_model_switch_accepts_committed_openai_responses_reasoning();
void test_application_catalog_cache_reuses_workspace_and_session_indexes();
void test_application_catalog_coordinator_serializes_refresh_and_snapshot();
void test_application_catalog_current_session_incremental_refresh();
void test_app_runtime_model_switch_persists_and_reopens();
void test_app_runtime_model_switch_projects_incompatible_history_at_request_time();
void test_app_runtime_reasoning_selection_persists_and_requests();
void test_app_runtime_branch_construction_failure_rolls_back_created_file();
void test_app_runtime_initial_reasoning_level_option();

void app_command_dispatcher_ui_part(ava::app::runtime::Session* session, ava::config::XdgPaths const& paths, std::filesystem::path const& workspace,
                                    std::vector<ava::app::CommandHotkey> const& custom_hotkeys);
void app_command_dispatcher_catalog_part(ava::app::runtime::Session* session, ava::config::XdgPaths const& paths, std::filesystem::path const& workspace,
                                         std::vector<ava::app::CommandHotkey> const& custom_hotkeys);
void app_command_dispatcher_auth_part(ava::app::runtime::Session* session, std::string const& plan_system_prompt);
void app_command_dispatcher_tool_part(ava::app::runtime::Session* session, std::filesystem::path const& workspace);
void app_command_dispatcher_session_part(ava::app::runtime::Session* session, std::filesystem::path const& workspace);

}  // namespace ava::tests::app_runtime_tests
