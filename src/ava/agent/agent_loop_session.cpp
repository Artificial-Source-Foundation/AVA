#include "sys.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/tool_result.h"
#include "ava/agent/usage_accounting.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <utility>

namespace ava::agent {
namespace {

ava::core::VoidResult append_entry_with_id(ava::session::SessionStore& store, ava::session::EntryType type, std::string const& id, std::string data_json)
{
  if (!store.is_ephemeral())
  {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "persistent AgentLoop session writes require an append authority route"));
  }
  return store.append_ephemeral(
      ava::session::SessionEntry{.id = id, .parent_id = "", .type = type, .timestamp = ava::session::now_timestamp(), .data_json = std::move(data_json)});
}

ava::core::VoidResult append_entry(ava::session::SessionStore& store, ava::session::EntryType type, std::string data_json)
{
  return append_entry_with_id(store, type, ava::core::make_id("entry"), std::move(data_json));
}

ava::core::VoidResult append_entry(SessionAppendSink const& sink, ava::session::EntryType type, std::string const& id, std::string data_json)
{
  if (!sink)
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "active session append route is required"));
  return sink(
      ava::session::SessionEntry{.id = id, .parent_id = "", .type = type, .timestamp = ava::session::now_timestamp(), .data_json = std::move(data_json)});
}

std::string reasoning_block_data_json(ParsedReasoningBlock const& block, std::string_view provider_id, std::string_view model_id)
{
  std::string json = "{\"provider\":\"" + ava::core::json::escape(provider_id) + "\",\"model\":\"" + ava::core::json::escape(model_id) + "\"";
  if (!block.format.empty())
    json += ",\"format\":\"" + ava::core::json::escape(block.format) + "\"";
  if (!block.text.empty())
    json += ",\"text\":\"" + ava::core::json::escape(block.text) + "\"";
  if (!block.signature.empty())
    json += ",\"signature\":\"" + ava::core::json::escape(block.signature) + "\"";
  if (!block.redacted_data.empty())
  {
    json += ",\"redacted_data\":\"" + ava::core::json::escape(block.redacted_data) + "\"";
  }
  json += ",\"redacted\":";
  json += block.redacted ? "true" : "false";
  json += '}';
  return json;
}

std::string image_attachments_json(std::vector<ava::session::ImageAttachmentRef> const& attachments)
{
  std::string json;
  json += '[';
  for (std::size_t index = 0; index < attachments.size(); ++index)
  {
    auto const& attachment = attachments[index];
    if (index > 0)
      json += ',';
    json += "{\"id\":\"" + ava::core::json::escape(attachment.id) + "\",\"type\":\"image\",\"mime_type\":\"" + ava::core::json::escape(attachment.mime_type) +
            "\",\"byte_size\":" + std::to_string(attachment.byte_size) + ",\"sha256\":\"" + ava::core::json::escape(attachment.sha256) +
            "\",\"storage_path\":\"" + ava::core::json::escape(attachment.storage_path) + "\"}";
  }
  json += ']';
  return json;
}

std::string user_message_data_json(std::string const& text, std::vector<ava::session::ImageAttachmentRef> const& attachments)
{
  std::string json = "{\"text\":\"" + ava::core::json::escape(text) + "\"";
  if (!attachments.empty())
    json += ",\"attachments\":" + image_attachments_json(attachments);
  json += '}';
  return json;
}

}  // namespace

ava::core::Result<std::string> append_user_message(ava::session::SessionStore& store, std::string const& text)
{
  return append_user_message(store, text, {});
}

ava::core::Result<std::string> append_user_message(ava::session::SessionStore& store, std::string const& text,
                                                   std::vector<ava::session::ImageAttachmentRef> const& attachments)
{
  auto id = ava::core::make_id("entry");
  auto appended = append_entry_with_id(store, ava::session::EntryType::UserMessage, id, user_message_data_json(text, attachments));
  if (!appended)
    return std::unexpected(std::move(appended.error()));
  return id;
}

ava::core::VoidResult append_replay_user_message(ava::session::SessionStore& store, std::string const& text, std::string const& replay_of)
{
  return append_replay_user_message(store, text, {}, replay_of);
}

ava::core::VoidResult append_replay_user_message(ava::session::SessionStore& store, std::string const& text,
                                                 std::vector<ava::session::ImageAttachmentRef> const& attachments, std::string const& replay_of)
{
  auto data = user_message_data_json(text, attachments);
  data.pop_back();
  data += ",\"internal_replay\":true,\"replay_of\":\"" + ava::core::json::escape(replay_of) + "\",\"reason\":\"context_compaction_active_prompt_replay\"}";
  return append_entry(store, ava::session::EntryType::UserMessage, std::move(data));
}

ava::core::VoidResult append_assistant_message(ava::session::SessionStore& store, std::string const& text, std::size_t tool_call_count,
                                               ava::provider::TokenUsage const& usage, std::optional<long double> const& cost_usd)
{
  return append_entry(store, ava::session::EntryType::AssistantMessage,
                      "{\"text\":\"" + ava::core::json::escape(text) + "\",\"tool_calls\":" + std::to_string(tool_call_count) +
                          ",\"usage\":" + usage_json(usage, cost_usd) + "}");
}

ava::core::VoidResult append_reasoning_block(ava::session::SessionStore& store, ParsedReasoningBlock const& block, std::string_view provider_id,
                                             std::string_view model_id)
{
  if (block.text.empty() && block.signature.empty() && block.redacted_data.empty())
    return {};
  return append_entry(store, ava::session::EntryType::ReasoningBlock, reasoning_block_data_json(block, provider_id, model_id));
}

ava::core::VoidResult append_tool_call(ava::session::SessionStore& store, ProviderToolCall const& call)
{
  return append_entry(store, ava::session::EntryType::ToolCall,
                      "{\"call_id\":\"" + ava::core::json::escape(call.id) + "\",\"name\":\"" + ava::core::json::escape(call.name) + "\",\"arguments\":\"" +
                          ava::core::json::escape(call.arguments_json) + "\"}");
}

ava::core::VoidResult append_tool_result(ava::session::SessionStore& store, ToolDispatchResult const& result)
{
  auto const materialized = with_tool_result_payload(result);
  return append_entry(store, ava::session::EntryType::ToolResult,
                      "{\"call_id\":\"" + ava::core::json::escape(materialized.call_id) + "\",\"name\":\"" + ava::core::json::escape(materialized.name) +
                          "\",\"success\":" + (materialized.success ? std::string("true") : std::string("false")) + ",\"status\":\"" +
                          ava::core::json::escape(to_string(materialized.payload.status)) + "\",\"result\":\"" +
                          ava::core::json::escape(materialized.result_text) + "\",\"structured_result\":" + serialize_tool_result_payload_json(materialized) +
                          "}");
}

ava::core::VoidResult append_permission_decision(ava::session::SessionStore& store, ava::tools::PermissionAuditEvent const& event)
{
  return append_entry(store, ava::session::EntryType::PermissionDecision, ava::tools::permission_audit_data_json(event));
}

ava::core::VoidResult append_error(ava::session::SessionStore& store, ava::core::Error const& error)
{
  return append_entry(store, ava::session::EntryType::Error,
                      "{\"category\":\"" + ava::core::json::escape(ava::core::to_string(error.category())) + "\",\"message\":\"" +
                          ava::core::json::escape(error.message()) + "\",\"details\":\"" + ava::core::json::escape(error.format()) + "\"}");
}

ava::core::VoidResult append_cancel(ava::session::SessionStore& store, std::string_view boundary)
{
  return append_entry(store, ava::session::EntryType::Cancel, "{\"reason\":\"cancel_requested\",\"boundary\":\"" + ava::core::json::escape(boundary) + "\"}");
}

ava::core::Result<std::string> append_user_message(SessionAppendSink const& sink, std::string const& text,
                                                   std::vector<ava::session::ImageAttachmentRef> const& attachments)
{
  auto id = ava::core::make_id("entry");
  auto appended = append_entry(sink, ava::session::EntryType::UserMessage, id, user_message_data_json(text, attachments));
  if (!appended)
    return std::unexpected(std::move(appended.error()));
  return id;
}

ava::core::VoidResult append_replay_user_message(SessionAppendSink const& sink, std::string const& text,
                                                 std::vector<ava::session::ImageAttachmentRef> const& attachments, std::string const& replay_of)
{
  auto data = user_message_data_json(text, attachments);
  data.pop_back();
  data += ",\"internal_replay\":true,\"replay_of\":\"" + ava::core::json::escape(replay_of) + "\",\"reason\":\"context_compaction_active_prompt_replay\"}";
  return append_entry(sink, ava::session::EntryType::UserMessage, ava::core::make_id("entry"), std::move(data));
}

ava::core::VoidResult append_assistant_message(SessionAppendSink const& sink, std::string const& text, std::size_t tool_call_count,
                                               ava::provider::TokenUsage const& usage, std::optional<long double> const& cost_usd)
{
  return append_entry(sink, ava::session::EntryType::AssistantMessage, ava::core::make_id("entry"),
                      "{\"text\":\"" + ava::core::json::escape(text) + "\",\"tool_calls\":" + std::to_string(tool_call_count) +
                          ",\"usage\":" + usage_json(usage, cost_usd) + "}");
}

ava::core::VoidResult append_reasoning_block(SessionAppendSink const& sink, ParsedReasoningBlock const& block, std::string_view provider_id,
                                             std::string_view model_id)
{
  if (block.text.empty() && block.signature.empty() && block.redacted_data.empty())
    return {};
  return append_entry(sink, ava::session::EntryType::ReasoningBlock, ava::core::make_id("entry"), reasoning_block_data_json(block, provider_id, model_id));
}

ava::core::VoidResult append_tool_call(SessionAppendSink const& sink, ProviderToolCall const& call)
{
  return append_entry(sink, ava::session::EntryType::ToolCall, ava::core::make_id("entry"),
                      "{\"call_id\":\"" + ava::core::json::escape(call.id) + "\",\"name\":\"" + ava::core::json::escape(call.name) + "\",\"arguments\":\"" +
                          ava::core::json::escape(call.arguments_json) + "\"}");
}

ava::core::VoidResult append_tool_result(SessionAppendSink const& sink, ToolDispatchResult const& result)
{
  auto const materialized = with_tool_result_payload(result);
  return append_entry(sink, ava::session::EntryType::ToolResult, ava::core::make_id("entry"),
                      "{\"call_id\":\"" + ava::core::json::escape(materialized.call_id) + "\",\"name\":\"" + ava::core::json::escape(materialized.name) +
                          "\",\"success\":" + (materialized.success ? std::string("true") : std::string("false")) + ",\"status\":\"" +
                          ava::core::json::escape(to_string(materialized.payload.status)) + "\",\"result\":\"" +
                          ava::core::json::escape(materialized.result_text) + "\",\"structured_result\":" + serialize_tool_result_payload_json(materialized) +
                          "}");
}

ava::core::VoidResult append_permission_decision(SessionAppendSink const& sink, ava::tools::PermissionAuditEvent const& event)
{
  return append_entry(sink, ava::session::EntryType::PermissionDecision, ava::core::make_id("entry"), ava::tools::permission_audit_data_json(event));
}

ava::core::VoidResult append_error(SessionAppendSink const& sink, ava::core::Error const& error)
{
  return append_entry(sink, ava::session::EntryType::Error, ava::core::make_id("entry"),
                      "{\"category\":\"" + ava::core::json::escape(ava::core::to_string(error.category())) + "\",\"message\":\"" +
                          ava::core::json::escape(error.message()) + "\",\"details\":\"" + ava::core::json::escape(error.format()) + "\"}");
}

ava::core::VoidResult append_cancel(SessionAppendSink const& sink, std::string_view boundary)
{
  return append_entry(sink, ava::session::EntryType::Cancel, ava::core::make_id("entry"),
                      "{\"reason\":\"cancel_requested\",\"boundary\":\"" + ava::core::json::escape(boundary) + "\"}");
}

}  // namespace ava::agent
