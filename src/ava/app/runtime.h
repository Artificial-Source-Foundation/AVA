#pragma once

#include "ava/app/events.h"
#include "ava/app/project_trust.h"
#include "ava/app/session_run_controller.h"
#include "ava/agent/agent_loop.h"
#include "ava/mcp/config.h"
#include "ava/config/model_config.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/session/attachments.h"
#include "ava/session/compaction.h"
#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"
#include "ava/permissions/permission.h"
#include "ava/provider/provider.h"
#include "ava/context/context_loader.h"
#include "ava/core/error.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "debug.h"

namespace ava::app {

enum class RuntimeFreshnessSourceKind
{
  SystemPrompt,
  AppendSystemPrompt,
  PromptCommand,
  Skill,
  PluginManifest,
  PluginPrompt,
  PluginSkill,
};

struct ContextSourceMetadata
{
  std::filesystem::path path;
  ava::context::ContextSourceType source_type = ava::context::ContextSourceType::Workspace;
  std::size_t byte_count = 0;
  std::uint64_t content_fingerprint = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RuntimeFreshnessSourceMetadata
{
  RuntimeFreshnessSourceKind kind = RuntimeFreshnessSourceKind::Skill;
  std::string scope;
  std::string source_id;
  std::string name;
  std::filesystem::path path;
  std::size_t byte_count = 0;
  std::uint64_t content_fingerprint = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RuntimePromptOverrides
{
  std::optional<std::string> system_prompt = std::nullopt;
  std::vector<std::string> append_system_prompts;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RuntimeOpenOptions
{
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
  std::optional<std::string> requested_session_id;
  std::optional<std::string> fork_session_id;
  std::optional<std::string> initial_session_name;
  bool continue_last_session = false;
  bool sessionless = false;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::agent::ToolVisibilityOptions tool_visibility;
  ava::config::XdgPaths paths = ava::config::xdg_paths();
  RuntimePromptOverrides prompt_overrides;
  std::optional<std::string> initial_reasoning_level = std::nullopt;
  // Strict long-lived adapters may pin the startup default so later config
  // edits cannot drift immutable connection capabilities from new sessions.
  std::optional<ava::config::ModelInfo> default_model_override = std::nullopt;
  // When true, the override is the complete immutable model snapshot for both
  // new and resumed sessions. No later model-registry read or persisted model
  // change may replace its provider, model, or capability metadata.
  bool pin_model_override = false;
  bool offline = false;
  // ACP and other strict adapters opt out of CLI prefix resolution and may
  // bound persistence reads without changing legacy CLI/RPC behavior.
  bool exact_session_id = false;
  std::optional<ava::session::SessionReadLimits> session_read_limits = std::nullopt;
  // Strict adapters may pin the persisted cwd while retaining lease acquisition
  // inside the protocol-neutral runtime ownership boundary.
  std::optional<std::filesystem::path> expected_original_cwd = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RuntimeReasoningSelection
{
  std::string level;
  std::optional<std::string> provider_level = std::nullopt;
  std::optional<long long> budget_tokens = std::nullopt;
  std::string display;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RuntimeBasePromptMetadata
{
  bool from_override = false;
  std::optional<std::filesystem::path> source_path = std::nullopt;
  std::size_t byte_count = 0;
  std::uint64_t content_fingerprint = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RuntimeSession
{
  ava::session::SessionStore store;
  // Every persistent runtime owner holds the same cross-process lease for the
  // complete RuntimeSession lifetime. Ephemeral sessions keep an empty lease.
  ava::session::SessionLease lease;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::config::ModelInfo model;
  RuntimeBasePromptMetadata base_prompt;
  ava::config::XdgPaths paths;
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir;
  ProjectTrustState project_trust;
  RuntimePromptOverrides prompt_overrides;
  ava::agent::ToolVisibilityOptions tool_visibility;
  std::vector<ContextSourceMetadata> context_sources;
  std::vector<RuntimeFreshnessSourceMetadata> freshness_sources;
  std::string system_prompt;
  std::optional<RuntimeReasoningSelection> reasoning = std::nullopt;
  std::optional<std::vector<std::string>> scoped_model_cycle = std::nullopt;
  bool created = false;
  bool sessionless = false;
  // Declare before workers: reverse destruction stops/joins background work
  // before destroying the controller and its SessionStore routes.
  std::unique_ptr<SessionRunController> run_controller = std::make_unique<SessionRunController>();
  std::shared_ptr<ava::agent::BackgroundJobRegistry> background_jobs = std::make_shared<ava::agent::BackgroundJobRegistry>();
  // Immutable, adapter-supplied MCP composition. Null means use normal
  // global/project discovery; non-null (including empty) is session-local.
  std::shared_ptr<ava::mcp::McpConfig const> mcp_config = nullptr;
  bool offline = false;

  // Every attached runtime session owns an append controller. Direct store
  // writes are reserved for detached import/export/new-session stores.
  [[nodiscard]] ava::core::VoidResult append_owned(ava::session::SessionEntry entry)
  {
    if (!run_controller)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime session controller is unavailable"));
    return run_controller->append(store, std::move(entry));
  }
  [[nodiscard]] ava::agent::SessionAppendSink owner_append_route()
  {
    return run_controller ? run_controller->owner_append_route(store) : ava::agent::SessionAppendSink{};
  }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RuntimePromptState
{
  ava::agent::Mode mode = ava::agent::Mode::Build;
  RuntimeBasePromptMetadata base_prompt;
  std::vector<ContextSourceMetadata> context_sources;
  std::vector<RuntimeFreshnessSourceMetadata> freshness_sources;
  std::string system_prompt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct RuntimeRunOptions
{
  // Caller supplied correlation ID. Generated by run_prompt only when absent.
  std::optional<std::string> request_id = std::nullopt;
  std::string access_token;
  std::string credential_type = "bearer";
  bool openai_oauth = false;
  std::string openai_account_id;
  bool stream = true;
  bool enable_transport_retries = false;
  // When present, compose exactly these built-ins with the immutable session
  // MCP configuration. An empty list exposes session MCP tools only.
  std::optional<std::vector<std::string>> exact_builtin_tool_names = std::nullopt;
  // Isolate the run from ambient project/plugin/LSP/subagent resources.
  bool isolate_project_resources = false;
  // Require descriptor-anchored workspace operations for selected built-ins.
  bool require_descriptor_secure_workspace = false;
  // Expose a pending -> in-progress boundary after permission succeeds.
  bool announce_execution_after_permission = false;
  bool redact_permission_audit_arguments = false;
  bool require_explicit_file_permissions = false;
  std::shared_ptr<ava::tools::ExactFileAccess const> exact_file_access = nullptr;
  std::shared_ptr<ava::tools::CommandExecutor const> command_executor = nullptr;
  RuntimeEventSink event_sink = nullptr;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  ava::agent::QuestionResolver question_resolver = nullptr;
  std::function<bool()> cancel_requested = nullptr;
  std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages = nullptr;
  // The active generation route is supplied by run_prompt and used by all
  // compaction records in that run.
  ava::agent::SessionAppendSink active_append_route = nullptr;
  std::mutex* session_mutex = nullptr;
  std::vector<ava::session::ImageAttachmentRef> image_attachments;
  // Disabled by default; trace data never enters RuntimeEvent/session/RPC.
  std::shared_ptr<ava::observability::RunObservation> observation = nullptr;
  ava::observability::TraceContext trace_context = {};
  // Optional adapter arbitration invoked immediately before the controller's
  // Completing transition. A failure means cancellation won and prevents the
  // durable terminal assistant boundary. ACP uses this; RPC leaves it unset.
  std::function<ava::core::VoidResult()> on_terminal_commit = nullptr;
  // Runs after the controller commits each phase. Kept protocol-neutral so
  // adapters and deterministic lifecycle tests observe the same authority.
  std::function<ava::core::VoidResult(RunPhase)> on_phase = nullptr;
  bool offline = false;
  bool expand_prompt_file_references = true;

  // Contains provider credentials and runtime callbacks; generated debug output
  // must never serialize this aggregate.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

using CompactionSummaryGenerator =
    std::function<ava::core::Result<std::string>(std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
                                                 std::string_view instructions, std::size_t estimated_tokens)>;

[[nodiscard]] ava::core::Result<RuntimeSession> open_runtime_session(RuntimeOpenOptions const& options);
// Builds a normal runtime around an already-owned persistent store. The store
// and lease are consumed only on success, so callers can perform identity-safe
// rollback after a failed handoff.
[[nodiscard]] ava::core::Result<RuntimeSession> open_owned_runtime_session(RuntimeOpenOptions const& options, ava::session::SessionStore& store,
                                                                           ava::session::SessionLease& lease, bool created);

[[nodiscard]] ava::core::Result<RuntimePromptState> select_runtime_prompt_state(RuntimeSession const& session, ava::agent::Mode mode);

void apply_runtime_prompt_state(RuntimeSession& session, RuntimePromptState prompt_state);

// Session-mutating control commands use the stable owner route so they remain
// serialized with an overlapping active run; import/export/new stores remain
// direct inactive operations.
[[nodiscard]] ava::core::Result<ava::session::SessionMetadataView> append_runtime_session_metadata(RuntimeSession& session,
                                                                                                   ava::session::SessionMetadataUpdate update);
[[nodiscard]] ava::core::VoidResult append_runtime_mode_change(RuntimeSession& session, ava::agent::Mode mode);

[[nodiscard]] ava::core::Result<ava::config::ModelInfo> resolve_runtime_model(ava::config::XdgPaths const& paths, std::string_view provider_id,
                                                                              std::string_view model_id);

[[nodiscard]] ava::core::Result<bool> switch_runtime_model(RuntimeSession& session, ava::config::ModelInfo model);

[[nodiscard]] ava::core::Result<bool> set_runtime_reasoning(RuntimeSession& session, std::optional<RuntimeReasoningSelection> selection);

// Stops background jobs, retires their owner routes, then replaces the store.
// Never use memberwise RuntimeSession move assignment for an attached session.
[[nodiscard]] ava::core::VoidResult replace_runtime_session(RuntimeSession& destination, RuntimeSession replacement);

[[nodiscard]] ava::core::Result<ava::agent::AgentLoopResult> run_prompt(RuntimeSession& session, std::string const& user_message,
                                                                        ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                                                        RuntimeRunOptions const& options);
// Strict adapters may admit before provider setup so cancellation covers that
// phase. The guard must come from this session's controller and request id.
[[nodiscard]] ava::core::Result<ava::agent::AgentLoopResult> run_admitted_prompt(RuntimeSession& session, std::string const& user_message,
                                                                                 ava::provider::Provider const& provider, ava::provider::Transport& transport,
                                                                                 RuntimeRunOptions const& options, ActiveRunGuard guard);

[[nodiscard]] ava::core::Error offline_provider_error(std::string_view action);

[[nodiscard]] bool same_session_snapshot(std::vector<ava::session::SessionEntry> const& expected, std::vector<ava::session::SessionEntry> const& actual);

[[nodiscard]] ava::core::Error stale_compaction_snapshot_error(std::string_view trigger, std::size_t snapshot_entries, std::size_t current_entries);

[[nodiscard]] std::string build_compaction_summary_prompt(std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
                                                          std::string_view instructions, std::size_t estimated_tokens);

[[nodiscard]] ava::core::Result<std::string> generate_compaction_summary(RuntimeSession const& session, std::vector<ava::session::SessionEntry> const& entries,
                                                                         ava::session::CompactionConfig const& config, std::string_view instructions,
                                                                         std::size_t estimated_tokens, ava::provider::Provider const& provider,
                                                                         ava::provider::Transport& transport, RuntimeRunOptions const& options);

[[nodiscard]] std::string to_string(RuntimeFreshnessSourceKind kind);

}  // namespace ava::app
