#pragma once

#include "ava/observability/run_observer.h"
#include "ava/agent/mode.h"
#include "ava/agent/question.h"
#include "ava/agent/subagent_config.h"
#include "ava/agent/tool_visibility.h"
#include "ava/tools/tool_io.h"
#include "ava/permissions/permission.h"
#include "ava/core/AnchorSet.h"
#include "ava/core/result.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ava::lsp {
class DiagnosticsProvider;
}  // namespace ava::lsp

namespace ava::mcp {
struct McpConfig;
}  // namespace ava::mcp

namespace ava::tools {

class MutationQueue;
class SecureWorkspace;

struct PermissionAuditEvent
{
  std::string permission_request_id = {};
  ava::permissions::Operation operation;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  std::string tool_name;
  ava::permissions::PermissionAction action = ava::permissions::PermissionAction::Deny;
  std::string reason;
  ava::permissions::PermissionRisk risk = ava::permissions::PermissionRisk::Low;
  std::filesystem::path target_path = {};
  std::string command;
  std::string resolution;
  std::string resolution_source;
  std::string resolution_reason;
  std::string actor = "agent";
  std::string rule_id;
  // Durable audit serialization must suppress command and recipe-display
  // fields derived from arguments when a strict frontend supplied redacted args.
  bool command_arguments_redacted = false;
  std::optional<ava::permissions::CommandPermissionMetadata> command_metadata = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

using PermissionAuditSink = std::function<ava::core::VoidResult(PermissionAuditEvent const&)>;

struct ToolProgressEvent
{
  std::string text;
  std::string call_id;
  std::string tool_name;
  std::string status = "running";

  AVA_DEBUG_PRINT_MEMBERS_ON
};

using ToolProgressSink = std::function<ava::core::VoidResult(ToolProgressEvent const&)>;

struct TaskSubagentRequest
{
  std::string description;
  std::string prompt;
  std::string subagent_type;
  std::string subagent_system_prompt;
  ava::agent::SubagentToolPreset tool_preset = ava::agent::SubagentToolPreset::Inherit;
  std::optional<std::string> task_id = std::nullopt;
  std::string command;
  bool background = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TaskSubagentResult
{
  std::string task_id;
  std::string job_id;
  std::filesystem::path session_path;
  std::string subagent_type;
  std::string state = "completed";
  std::string final_text;
  std::string stop_reason;
  std::size_t provider_iterations = 0;
  std::size_t tool_calls = 0;
  std::size_t tool_iterations = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

using TaskSubagentRunner = std::function<ava::core::Result<TaskSubagentResult>(TaskSubagentRequest const&)>;

struct ToolContext
{
  std::filesystem::path workspace_dir;
  std::filesystem::path spill_dir = {};
  ava::agent::Mode mode = ava::agent::Mode::Build;
  // Local sealed command execution is enabled by default. Legacy and
  // PromptOnly contexts remain explicit non-executing compatibility modes.
  ava::command::CommandRuntimeOptions command_runtime{.mode = ava::command::CommandRuntimeMode::Enabled};
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  // Deny-only, non-interactive policy check used before a command's backend
  // auto-Allow. It must never prompt or return reusable authority.
  ava::permissions::PermissionResolver command_deny_preflight = nullptr;
  PermissionAuditSink permission_audit_sink = nullptr;
  ToolProgressSink progress_sink = nullptr;
  // Strict adapters may expose a distinct pending -> in_progress boundary.
  // The shared flag keeps multi-permission tools to one execution-start event.
  bool announce_execution_after_permission = false;
  std::shared_ptr<std::atomic_bool> execution_started = nullptr;
  std::function<bool()> cancel_requested = nullptr;
  ava::agent::QuestionResolver question_resolver = nullptr;
  TaskSubagentRunner task_subagent_runner = nullptr;
  std::vector<ava::agent::SubagentDefinition> subagents = {};
  std::string permission_tool_name = {};
  std::string permission_actor = {};
  std::string current_tool_name = {};
  std::string current_call_id = {};
  // Strict adapters can keep exact arguments in memory for matching while
  // preventing them from entering durable permission audit records.
  bool redact_permission_audit_arguments = false;
  bool require_explicit_file_permissions = false;
  // Strict adapters share one descriptor-anchored root across permission
  // identity resolution and the actual built-in file operation.
  std::shared_ptr<SecureWorkspace> secure_workspace = nullptr;
  // Pre-opened anchor descriptors for all writable directories (workspace,
  // spill, session storage, synthetic command roots, and any user-configured
  // additional dirs). Path authority is selected lexically and resolved
  // descriptor-relative; configured anchor roots may themselves contain
  // symlink components.
  std::shared_ptr<ava::core::AnchorSet> anchor_set = nullptr;
  // Actual AVA config/state/sessions/auth authority directories supplied from
  // the runtime session. These are passed to command sealing so workspace
  // overlap with authority roots is rejected, and to containment so authority
  // roots are never made writable through a broader workspace rule. Direct
  // test contexts may leave this empty.
  std::vector<std::filesystem::path> ava_authority_roots = {};
  // ToolContext is copied into dispatchers/workers, so immutable adapters share
  // session ownership rather than storing lifetime-sensitive references.
  std::shared_ptr<ExactFileAccess const> exact_file_access = nullptr;
  std::shared_ptr<CommandExecutor const> command_executor = nullptr;
  // An observer-only correlation ID. Provider call IDs remain product/session
  // data and must not cross the trace boundary.
  std::string trace_call_id = {};
  std::shared_ptr<std::vector<std::string>> permission_request_ids = nullptr;
  std::shared_ptr<MutationQueue> mutation_queue = nullptr;
  std::shared_ptr<ava::lsp::DiagnosticsProvider> lsp_diagnostics_provider = nullptr;
  std::filesystem::path plugin_global_plugins_dir = {};
  std::filesystem::path plugin_project_plugins_dir = {};
  std::filesystem::path plugin_enablement_file = {};
  bool include_project_plugins = true;
  std::filesystem::path mcp_global_config_file = {};
  std::filesystem::path mcp_project_config_file = {};
  bool include_project_mcp_config = true;
  std::shared_ptr<ava::mcp::McpConfig const> session_mcp_config = nullptr;
  // Present means compose exactly these built-ins with immutable session MCP
  // and fail closed on unavailable names, discovery failures, or collisions.
  std::optional<std::vector<std::string>> exact_builtin_tool_names = std::nullopt;
  bool require_descriptor_secure_workspace = false;
  std::vector<std::filesystem::path> skill_global_dirs = {};
  std::vector<std::filesystem::path> skill_project_dirs = {};
  bool include_project_skills = true;
  std::string session_id = {};
  std::string provider_id = {};
  std::string model_id = {};
  std::filesystem::path current_dir = {};
  ava::agent::ToolVisibilityOptions tool_visibility = {};
  std::shared_ptr<ava::observability::RunObservation> observation = nullptr;
  ava::observability::TraceContext trace_context = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TextOutput
{
  std::string content;
  bool truncated = false;
  bool byte_limited = false;
  bool line_limited = false;
  bool totals_known = true;
  std::size_t total_bytes = 0;
  std::size_t output_bytes = 0;
  std::size_t output_lines = 0;
  std::size_t start_line = 1;
  std::size_t end_line = 0;
  std::size_t total_lines = 0;
  std::size_t next_offset_line = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct FileMutationResult
{
  std::filesystem::path path;
  std::size_t bytes_written = 0;
  std::string diff;
  bool diff_truncated = false;
  std::string line_endings;
  bool had_utf8_bom = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ReadOptions
{
  std::size_t max_bytes = 50 * 1024;
  std::size_t offset_line = 1;
  std::size_t max_lines = 200;
  bool permission_already_checked = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct WriteOptions
{
  bool permission_already_checked = false;
  bool mutation_already_locked = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::Result<TextOutput> read_file(ToolContext const& context, std::filesystem::path const& path, ReadOptions options = {});
[[nodiscard]] ava::core::Result<FileMutationResult> write_file(ToolContext const& context, std::filesystem::path const& path, std::string_view content,
                                                               WriteOptions options = {});
[[nodiscard]] ava::core::Result<FileMutationResult> edit_file(ToolContext const& context, std::filesystem::path const& path, std::string_view old_text,
                                                              std::string_view new_text);
[[nodiscard]] ava::core::VoidResult announce_tool_execution_start(ToolContext const& context);
[[nodiscard]] ava::core::VoidResult ensure_permission(ToolContext const& context, ava::permissions::Operation operation,
                                                      std::filesystem::path const& target_path, std::string_view command, std::string_view tool_name,
                                                      std::string_view error_message, std::string_view diff_preview = {}, bool diff_truncated = false,
                                                      std::optional<ava::permissions::CommandPermissionMetadata> command_metadata = std::nullopt);
// Command approval receives an already prepared plan. The caller retains that
// exact preparation through execution, so resolver, audit, and executor share
// one sealed identity rather than reparsing compatibility text.
[[nodiscard]] ava::core::VoidResult ensure_command_permission(ToolContext const& context, std::string_view command,
                                                              ava::command::CommandPreparation const& preparation, bool unverified_delegated_executor,
                                                              std::string_view tool_name, std::string_view error_message);
[[nodiscard]] ava::core::VoidResult ensure_command_permission(ToolContext const& context, std::string_view command,
                                                              ava::command::CommandPreparation const& preparation,
                                                              ava::permissions::CommandContainmentInfo const& containment, bool unverified_delegated_executor,
                                                              std::string_view tool_name, std::string_view error_message);
[[nodiscard]] std::string permission_audit_data_json(PermissionAuditEvent const& event);
[[nodiscard]] ava::core::VoidResult replace_file_with_staged_file(std::filesystem::path const& staged_path, std::filesystem::path const& target_path);
void remove_staged_file_best_effort(std::filesystem::path const& staged_path);

}  // namespace ava::tools
