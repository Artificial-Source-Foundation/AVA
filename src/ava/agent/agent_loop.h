#pragma once

#include "ava/observability/run_observer.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/background_job_registry.h"
#include "ava/agent/message_builder.h"
#include "ava/agent/mode.h"
#include "ava/agent/question.h"
#include "ava/agent/run_phase.h"
#include "ava/agent/subagent_config.h"
#include "ava/agent/tool_visibility.h"
#include "ava/config/model_config.h"
#include "ava/session/attachments.h"
#include "ava/session/session_store.h"
#include "ava/permissions/permission.h"
#include "ava/provider/provider.h"
#include "ava/lsp/lsp_client.h"
#include "ava/core/result.h"
#include "ava/core/runtime_outcome.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ava::mcp {
struct McpConfig;
}

namespace ava::tools {
class ExactFileAccess;
class CommandExecutor;
} // namespace ava::tools

namespace ava::agent {

enum class ToolTimelineStatus
{
  Running,
  Success,
  Canceled,
  Error,
};

struct ToolTimelineEntry
{
  ToolTimelineStatus status = ToolTimelineStatus::Running;
  std::string call_id = {};
  std::string name = {};
  std::string argument_summary = {};
  std::string result_summary = {};
  std::string arguments_json = {};
  std::string result_json = {};
  std::string structured_result_json = {};
  std::string content_type = {};
  std::string error_category = {};
  std::string error_code = {};
  std::string error_message = {};
  std::string error_details = {};
  std::string diff = {};
  bool diff_truncated = false;
  std::vector<std::string> changed_paths = {};
  std::vector<std::string> permission_request_ids = {};
  bool truncated = false;
  bool byte_limited = false;
  bool line_limited = false;
  std::optional<std::size_t> output_bytes = std::nullopt;
  std::optional<std::size_t> total_bytes = std::nullopt;
  std::optional<std::size_t> output_lines = std::nullopt;
  std::optional<std::size_t> total_lines = std::nullopt;
  std::optional<std::size_t> start_line = std::nullopt;
  std::optional<std::size_t> end_line = std::nullopt;
  std::optional<std::size_t> next_offset_line = std::nullopt;
  std::optional<std::size_t> omitted_bytes = std::nullopt;
  std::optional<std::size_t> omitted_lines = std::nullopt;
  std::optional<std::size_t> visible_matches = std::nullopt;
  std::optional<std::size_t> total_matches = std::nullopt;
  std::string spill_path = {};
  bool spill_truncated = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ToolProgressEntry
{
  std::string call_id = {};
  std::string name = {};
  std::string text = {};
  std::string status = "running";

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] std::string to_string(ToolTimelineStatus status);

struct AgentLoopOptions
{
  std::filesystem::path workspace_dir;
  std::filesystem::path current_dir = {};
  Mode mode = Mode::Build;
  std::string provider_id = "openai";
  std::string model_id = "gpt-5.5";
  std::string system_prompt;
  std::string access_token;
  std::string credential_type = "bearer";
  bool openai_oauth = false;
  std::string openai_account_id = "";
  std::size_t max_tool_iterations = 10;
  std::size_t max_provider_events = 4096;
  std::size_t max_assistant_text_bytes = 256 * 1024;
  std::size_t max_tool_argument_bytes = 256 * 1024;
  std::size_t max_tool_result_context_bytes = 8 * 1024;
  bool stream = true;
  bool model_supports_tools = true;
  bool model_supports_streaming = true;
  bool include_project_resources = true;
  std::filesystem::path plugin_global_plugins_dir = {};
  std::filesystem::path plugin_project_plugins_dir = {};
  std::filesystem::path plugin_enablement_file = {};
  std::shared_ptr<ava::mcp::McpConfig const> session_mcp_config = nullptr;
  std::optional<std::vector<std::string>> exact_builtin_tool_names = std::nullopt;
  bool require_descriptor_secure_workspace = false;
  bool announce_execution_after_permission = false;
  bool redact_permission_audit_arguments = false;
  bool require_explicit_file_permissions = false;
  // Runtime-owned AVA config/state/session directories that command sealing
  // must keep disjoint from the model command workspace.
  std::vector<std::filesystem::path> ava_authority_roots = {};
  std::shared_ptr<ava::tools::ExactFileAccess const> exact_file_access = nullptr;
  std::shared_ptr<ava::tools::CommandExecutor const> command_executor = nullptr;
  std::vector<SubagentDefinition> subagents = {};
  ToolVisibilityOptions tool_visibility = {};
  std::vector<std::string> model_input_modalities = {"text"};
  std::optional<long long> model_max_output_tokens = std::nullopt;
  std::optional<ava::provider::ProviderReasoningOptions> reasoning = std::nullopt;
  std::function<void(ToolTimelineEntry const&)> on_tool_event = nullptr;
  std::function<ava::core::VoidResult(ToolProgressEntry const&)> on_tool_progress = nullptr;
  std::function<ava::core::VoidResult(ava::provider::StreamEvent const&)> on_stream_event = nullptr;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  // Deny-only, non-interactive policy check used before a model-initiated
  // command's backend auto-Allow. Mirrors ToolContext::command_deny_preflight
  // so model tool calls receive the same persistent-Deny enforcement as direct
  // app commands. It must never prompt or return reusable authority.
  ava::permissions::PermissionResolver command_deny_preflight = nullptr;
  QuestionResolver question_resolver = nullptr;
  std::function<bool()> cancel_requested = nullptr;
  std::function<ava::core::Result<std::vector<std::string>>()> take_steering_messages = nullptr;
  std::shared_ptr<ava::lsp::DiagnosticsProvider> lsp_diagnostics_provider = nullptr;
  std::function<ava::core::Result<bool>(ava::session::SessionReadAuthority, std::string_view, std::vector<std::string> const& replayed_user_messages)>
      compact_context = nullptr;
  std::function<ava::core::Result<std::unique_ptr<ava::provider::Provider>>()> background_provider_factory = nullptr;
  std::function<ava::core::Result<std::unique_ptr<ava::provider::Transport>>()> background_transport_factory = nullptr;
  std::shared_ptr<BackgroundJobRegistry> background_jobs = nullptr;
  std::mutex* session_mutex = nullptr;
  // Immutable generation routes for records produced by this run. Persistent
  // provider assistant turns require the batch route so v4 staging and its
  // commit are appended through one guarded authority.
  SessionAppendSink append_entry = nullptr;
  SessionAppendBatchSink append_batch = nullptr;
  // Copyable exact-lease (or in-memory) authority used for every history read.
  std::optional<ava::session::SessionReadAuthority> session_read_authority = std::nullopt;
  // Must match the policy established when the runtime session was opened.
  // Direct unit construction retains historical unbounded behavior.
  ava::session::SessionReadLimits session_read_limits = ava::session::legacy_unbounded_session_read_limits();
  // Stable session-owner route for parent notices produced by background
  // children. Unlike append_entry it remains valid between generations.
  SessionAppendSink parent_notification_sink = nullptr;
  // Called at real loop boundaries; errors abort the loop rather than being
  // swallowed as observer-only state.
  std::function<ava::core::VoidResult(RunPhase)> on_phase = nullptr;
  std::optional<ava::config::ModelPricing> model_pricing = std::nullopt;
  bool parallel_read_search_tools = false;
  std::size_t parallel_read_search_max_workers = 4;
  // Disabled by default. This is independent from runtime::Event/RPC output.
  std::shared_ptr<ava::observability::RunObservation> observation = nullptr;
  // Runtime may pre-establish this so retries, compaction, and the agent share
  // one run/turn identity.
  ava::observability::TraceContext trace_context = {};

  // Includes provider credentials and callback/runtime ownership state; never
  // stream this aggregate through generated debug output.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct AgentLoopResult
{
  std::string final_text;
  std::optional<ava::provider::TokenUsage> usage = std::nullopt;
  std::optional<long double> cost_usd = std::nullopt;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;
  std::size_t initial_context_messages = 0;
  bool used_compacted_context = false;
  std::size_t tool_iterations = 0;
  ava::core::RuntimeTerminalOutcome outcome = ava::core::RuntimeTerminalOutcome::Error;
  std::vector<ToolTimelineEntry> tool_timeline;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

class AgentLoop
{
 public:
  explicit AgentLoop(AgentLoopOptions options);

  [[nodiscard]] ava::core::Result<AgentLoopResult> run_turn(std::string const& user_message, ava::session::SessionStore& store,
                                                            ava::provider::Provider const& provider, ava::provider::Transport& transport);
  [[nodiscard]] ava::core::Result<AgentLoopResult> run_turn(std::string const& user_message,
                                                            std::vector<ava::session::ImageAttachmentRef> const& image_attachments,
                                                            ava::session::SessionStore& store, ava::provider::Provider const& provider,
                                                            ava::provider::Transport& transport);
  // Owns AgentLoopOptions, which contains provider credentials.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  [[nodiscard]] ava::core::Result<AgentLoopResult> run_turn_impl(std::string const& user_message,
                                                                 std::vector<ava::session::ImageAttachmentRef> const& image_attachments,
                                                                 ava::session::SessionStore& store, ava::provider::Provider const& provider,
                                                                 ava::provider::Transport& transport, ava::observability::TraceContext const& trace_context);

  AgentLoopOptions options_;
  bool ava_authority_roots_over_limit_ = false;
};

}  // namespace ava::agent
