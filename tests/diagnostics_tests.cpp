#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/diagnostics/safe_failure.h"
#include "ava/agent/message_builder.h"
#include "ava/session/logical_projection.h"
#include "ava/session/portable_sanitization.h"
#include "ava/core/json.h"

#include <array>
#include <string>
#include "debug.h"

namespace {

void test_closed_failure_serializers()
{
  constexpr std::array components{ava::diagnostics::ComponentClass::Mcp, ava::diagnostics::ComponentClass::Plugin};
  constexpr std::array categories{ava::diagnostics::FailureCategory::Configuration, ava::diagnostics::FailureCategory::Transport,
                                  ava::diagnostics::FailureCategory::Authorization, ava::diagnostics::FailureCategory::Protocol,
                                  ava::diagnostics::FailureCategory::Cancellation,  ava::diagnostics::FailureCategory::Internal};
  constexpr std::array codes{ava::diagnostics::FailureCode::InvalidRequest,  ava::diagnostics::FailureCode::IoFailure,
                             ava::diagnostics::FailureCode::NotFound,        ava::diagnostics::FailureCode::PermissionDenied,
                             ava::diagnostics::FailureCode::ExternalFailure, ava::diagnostics::FailureCode::Canceled,
                             ava::diagnostics::FailureCode::InternalFailure};
  constexpr std::array retryability{ava::diagnostics::Retryability::Never, ava::diagnostics::Retryability::AfterUserAction,
                                    ava::diagnostics::Retryability::Transient};
  constexpr std::array hints{ava::diagnostics::RecoveryHint::VerifyRequest, ava::diagnostics::RecoveryHint::VerifyConfiguration,
                             ava::diagnostics::RecoveryHint::VerifyPermissions, ava::diagnostics::RecoveryHint::RetryOperation,
                             ava::diagnostics::RecoveryHint::ContactSupport};

  for (auto const value : components) expect(!ava::diagnostics::to_string(value).empty(), "SafeFailure component enum has a fixed serializer");
  for (auto const value : categories) expect(!ava::diagnostics::to_string(value).empty(), "SafeFailure category enum has a fixed serializer");
  for (auto const value : codes) expect(!ava::diagnostics::to_string(value).empty(), "SafeFailure code enum has a fixed serializer");
  for (auto const value : retryability) expect(!ava::diagnostics::to_string(value).empty(), "SafeFailure retryability enum has a fixed serializer");
  for (auto const value : hints) expect(!ava::diagnostics::to_string(value).empty(), "SafeFailure recovery enum has a fixed serializer");

  auto const failure = ava::diagnostics::external_failure(ava::diagnostics::ComponentClass::Mcp);
  auto const json = ava::diagnostics::serialize_safe_failure_json(failure);
  auto const human = ava::diagnostics::serialize_safe_failure_human(failure);
  expect(ava::core::json::is_valid_object(json) && json.find("\"component\":\"mcp\"") != std::string::npos &&
             json.find("\"code\":\"external_failure\"") != std::string::npos,
         "SafeFailure JSON serializer emits only the stable closed representation");
  expect(human == "MCP integration operation failed [external_failure]. Verify the integration configuration before trying again.",
         "SafeFailure human serializer is stable and fixed");
}

void test_core_error_adapter_discards_raw_text()
{
  constexpr std::string_view canary = "CANARY_DIAGNOSTIC_SECRET_74a9";
  ava::core::Error error(ava::core::ErrorCategory::Io, std::string(canary));
  error.with_context("path", std::string(canary));
  auto const failure = ava::diagnostics::safe_failure_from_error(ava::diagnostics::ComponentClass::Plugin, error);
  auto const json = ava::diagnostics::serialize_safe_failure_json(failure);
  auto const human = ava::diagnostics::serialize_safe_failure_human(failure);
  expect(json.find(canary) == std::string::npos && human.find(canary) == std::string::npos,
         "SafeFailure adapter never serializes Error message or context text");
  expect(failure.code == ava::diagnostics::FailureCode::IoFailure && failure.retryability == ava::diagnostics::Retryability::Transient,
         "SafeFailure adapter classifies only the core ErrorCategory");
}

void test_external_tool_identity_boundary()
{
  auto const mcp = ava::diagnostics::external_tool_component("mcp_demo_echo");
  auto const plugin = ava::diagnostics::external_tool_component("plugin_demo_echo");
  expect(mcp == ava::diagnostics::ComponentClass::Mcp && plugin == ava::diagnostics::ComponentClass::Plugin,
         "reserved external-tool prefixes identify historical integration results");
  expect(!ava::diagnostics::external_tool_component("bash") && !ava::diagnostics::external_tool_component("xmcp_demo"),
         "unrelated built-in tool names are outside the fail-closed integration boundary");
}

void test_historical_external_failure_projection_is_safe()
{
  constexpr std::string_view canary = "CANARY_HISTORICAL_EXTERNAL_FAILURE_93c1";
  ava::session::SessionEntry call{.id = "entry_call",
                                  .parent_id = "",
                                  .type = ava::session::EntryType::ToolCall,
                                  .timestamp = "2026-01-01T00:00:00Z",
                                  .data_json = "{\"call_id\":\"call_external\",\"name\":\"mcp_demo_echo\",\"arguments\":\"{}\"}",
                                  .version = 3};
  ava::session::SessionEntry result{
      .id = "entry_result",
      .parent_id = "entry_call",
      .type = ava::session::EntryType::ToolResult,
      .timestamp = "2026-01-01T00:00:01Z",
      .data_json = "{\"call_id\":\"call_external\",\"name\":\"mcp_demo_echo\",\"success\":false,\"status\":\"error\",\"result\":\"" + std::string(canary) +
                   "\",\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_external\",\"tool\":\"mcp_demo_echo\",\"status\":\"error\","
                   "\"ok\":false,\"summary\":\"" +
                   std::string(canary) + "\",\"content_type\":\"text/plain\",\"content\":\"" + std::string(canary) +
                   "\",\"error\":{\"category\":\"tool\",\"message\":\"" + std::string(canary) + "\"},\"truncated\":false}}",
      .version = 3};
  std::vector<ava::session::SessionEntry> const entries{call, result};

  auto const replay = ava::agent::build_provider_messages_from_entries(entries);
  std::string replay_text;
  if (replay)
  {
    for (auto const& message : *replay)
    {
      replay_text += message.content;
      for (auto const& part : message.content_parts) replay_text += part.text;
    }
  }
  expect(replay && replay_text.find(canary) == std::string::npos && replay_text.find("external_failure") != std::string::npos,
         "provider replay replaces historical failed MCP content with SafeFailure");

  auto const public_projection = ava::session::project_logical_session_history(entries);
  auto const portable_projection = ava::session::project_portable_session_history(entries);
  auto const direct_portable = ava::session::sanitize_session_entry_for_portable_jsonl_export(result);
  expect(public_projection && public_projection->back().data_json.find(canary) == std::string::npos &&
             public_projection->back().data_json.find("external_failure") != std::string::npos,
         "public session projection removes historical failed MCP content");
  expect(portable_projection && portable_projection->back().data_json.find(canary) == std::string::npos &&
             portable_projection->back().data_json.find("external_failure") != std::string::npos && direct_portable.data_json.find(canary) == std::string::npos,
         "portable projection and direct portable sanitization remove historical failed MCP content");

  auto plugin_call = call;
  auto plugin_result = result;
  auto replace_tool_name = [](std::string& text) {
    constexpr std::string_view old_name = "mcp_demo_echo";
    constexpr std::string_view new_name = "plugin_demo_echo";
    for (auto offset = text.find(old_name); offset != std::string::npos; offset = text.find(old_name, offset + new_name.size()))
      text.replace(offset, old_name.size(), new_name);
  };
  replace_tool_name(plugin_call.data_json);
  replace_tool_name(plugin_result.data_json);
  std::vector<ava::session::SessionEntry> const plugin_entries{plugin_call, plugin_result};
  auto const plugin_replay = ava::agent::build_provider_messages_from_entries(plugin_entries);
  auto const plugin_public = ava::session::project_logical_session_history(plugin_entries);
  auto const plugin_portable = ava::session::project_portable_session_history(plugin_entries);
  std::string plugin_replay_text;
  if (plugin_replay)
  {
    for (auto const& message : *plugin_replay)
    {
      plugin_replay_text += message.content;
      for (auto const& part : message.content_parts) plugin_replay_text += part.text;
    }
  }
  expect(plugin_replay && plugin_public && plugin_portable && plugin_replay_text.find(canary) == std::string::npos &&
             plugin_public->back().data_json.find(canary) == std::string::npos && plugin_portable->back().data_json.find(canary) == std::string::npos,
         "provider replay and public/portable projections remove historical failed plugin content");

  auto successful = result;
  successful.data_json =
      "{\"call_id\":\"call_external\",\"name\":\"plugin_demo_echo\",\"success\":true,\"status\":\"success\",\"result\":\"" + std::string(canary) + "\"}";
  auto const preserved = ava::session::sanitize_session_entry_for_portable_jsonl_export(successful);
  expect(preserved.data_json.find(canary) != std::string::npos, "successful external-tool content remains unchanged by compatibility sanitization");
}

}  // namespace

void run_diagnostics_tests()
{
  test_closed_failure_serializers();
  test_core_error_adapter_discards_raw_text();
  test_external_tool_identity_boundary();
  test_historical_external_failure_projection_is_safe();
}
