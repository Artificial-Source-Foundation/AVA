#include "sys.h"
#include "ava/app/rpc/catalog.h"

#include <algorithm>
#include <array>

namespace ava::app::rpc {
namespace {

constexpr std::array<std::string_view, 58> kCommandTypes = {
    "get_protocol",
    "get_state",
    "prompt",
    "steer",
    "follow_up",
    "cancel",
    "permission_reply",
    "question_reply",
    "permission_grants",
    "permission_grant_revoke",
    "permission_grants_clear",
    "permission_rules",
    "permission_rule_add",
    "permission_rule_remove",
    "get_messages",
    "get_session_stats",
    "validate_session",
    "list_sessions",
    "session_tree",
    "session_metadata",
    "set_session_name",
    "set_session_labels",
    "new_session",
    "open_session",
    "switch_session",
    "fork_session",
    "clone_session",
    "summarize_branch",
    "list_models",
    "set_model",
    "cycle_model",
    "set_reasoning",
    "clear_reasoning",
    "run_bash",
    "run_command",
    "compact",
    "export",
    "export_html",
    "context",
    "list_commands",
    "invoke_command",
    "list_plugins",
    "plugin_failures",
    "inspect_plugin",
    "install_plugin",
    "remove_plugin",
    "enable_plugin",
    "disable_plugin",
    "validate_plugin",
    "list_plugin_prompts",
    "get_plugin_prompt",
    "list_plugin_skills",
    "get_plugin_skill",
    "run_plugin_command",
    "list_mcp_servers",
    "inspect_mcp_server",
    "list_mcp_tools",
    "restart_mcp_server",
};

constexpr std::array<std::string_view, 34> kEventNames = {
    "session_start",
    "user_message",
    "message_update",
    "message_end",
    "provider_event",
    "reasoning_start",
    "reasoning_delta",
    "reasoning_end",
    "assistant_message",
    "tool_start",
    "tool_progress",
    "tool_result",
    "compaction_start",
    "compaction_end",
    "retry",
    "retry_tick",
    "cancel_requested",
    "canceled",
    "error",
    "done",
    "permission_requested",
    "permission_replied",
    "permission_grant_revoked",
    "permission_grants_cleared",
    "permission_rule_added",
    "permission_rule_removed",
    "question_requested",
    "question_replied",
    "steer_queued",
    "steer_applied",
    "steer_skipped",
    "follow_up_queued",
    "follow_up_started",
    "follow_up_skipped",
};

constexpr std::array<std::string_view, 11> kStableErrorCodes = {
    "invalid_request",   "active_run",     "canceled",      "follow_up_skipped", "io_error",       "not_found",
    "permission_denied", "provider_error", "session_error", "tool_error",        "internal_error",
};

template <std::size_t Size>
bool contains(std::array<std::string_view, Size> const& values, std::string_view value) noexcept
{
  return std::ranges::find(values, value) != values.end();
}

}  // namespace

RpcProtocolVersions rpc_protocol_versions() noexcept
{
  return kRpcProtocolVersions;
}

std::span<std::string_view const> rpc_command_types() noexcept
{
  return kCommandTypes;
}

std::span<std::string_view const> rpc_event_names() noexcept
{
  return kEventNames;
}

std::span<std::string_view const> rpc_stable_error_codes() noexcept
{
  return kStableErrorCodes;
}

bool is_rpc_command_type(std::string_view type) noexcept
{
  return contains(kCommandTypes, type);
}

bool is_rpc_event_name(std::string_view name) noexcept
{
  return contains(kEventNames, name);
}

std::string_view stable_rpc_error_code(std::string_view candidate) noexcept
{
  return contains(kStableErrorCodes, candidate) ? candidate : std::string_view("internal_error");
}

}  // namespace ava::app::rpc
