#include "sys.h"
#include "ava/diagnostics/safe_failure.h"
#include "ava/session/portable_sanitization.h"
#include "ava/session/validation.h"
#include "ava/core/json.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ava::session {
namespace {

std::string json_string(std::string_view value)
{
  return "\"" + ava::core::json::escape(value) + "\"";
}

bool bool_field_is_true(SessionEntry const& entry, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(entry.data_json, key);
  return start && entry.data_json.substr(*start, 4) == "true";
}

SessionEntry sanitize_permission_decision_for_portable_jsonl_export(SessionEntry entry)
{
  auto const action = ava::core::json::string_field(entry.data_json, "action").value_or("");
  auto const resolution_source = ava::core::json::string_field(entry.data_json, "resolution_source").value_or("");
  if ((action != "allow" && action != "deny") || resolution_source == "policy")
    return entry;

  std::string data = "{";
  bool first = true;
  auto append = [&](std::string_view key, std::string_view value) {
    if (!first)
      data += ',';
    first = false;
    data += json_string(key);
    data += ':';
    data += json_string(value);
  };
  for (auto const key : {"permission_request_id", "operation", "mode", "tool_name"})
    append(key, ava::core::json::string_field(entry.data_json, key).value_or(""));
  append("action", action);
  for (auto const key : {"reason", "risk", "target_path", "command", "resolution", "resolution_reason", "actor", "rule_id"})
  {
    if (auto const value = ava::core::json::string_field(entry.data_json, key))
      append(key, *value);
  }
  append("resolution_source", "policy");
  data += '}';
  entry.data_json = std::move(data);
  return entry;
}

SessionEntry sanitize_user_message_attachments_for_portable_jsonl_export(SessionEntry entry)
{
  if (!ava::core::json::field_value_start(entry.data_json, "attachments"))
    return entry;

  auto const sanitized = sanitized_message_data_json(entry.data_json);
  auto const attachments = ava::core::json::objects_in_array_field(sanitized, "attachments");
  if (attachments.empty())
  {
    entry.data_json = sanitized;
    return entry;
  }

  std::string data = "{";
  bool first = true;
  auto append_key = [&](std::string_view key) {
    if (!first)
      data += ',';
    first = false;
    data += json_string(key);
    data += ':';
  };
  if (auto const text = ava::core::json::string_field(sanitized, "text"))
  {
    append_key("text");
    data += json_string(*text);
  }

  append_key("attachments");
  data += '[';
  bool first_attachment = true;
  for (auto const& attachment : attachments)
  {
    auto const id = ava::core::json::string_field(attachment, "id");
    auto const mime_type = ava::core::json::string_field(attachment, "mime_type");
    auto const byte_size = ava::core::json::integer_field(attachment, "byte_size");
    auto const sha256 = ava::core::json::string_field(attachment, "sha256");
    if (!id || !mime_type || !byte_size || !sha256)
      continue;
    if (!first_attachment)
      data += ',';
    first_attachment = false;
    data += "{\"id\":" + json_string(*id) + ",\"type\":\"image\",\"mime_type\":" + json_string(*mime_type) + ",\"byte_size\":" + std::to_string(*byte_size) +
            ",\"sha256\":" + json_string(*sha256) + ",\"storage_path\":\"attachments/portable-redacted\",\"redacted\":true}";
  }
  data += "]}";
  entry.data_json = std::move(data);
  return entry;
}

}  // namespace

std::string sanitized_tool_result_data_json(SessionEntry const& entry, bool preserve_output_binding)
{
  auto const call_id = ava::core::json::string_field(entry.data_json, "call_id").value_or("");
  auto const name = ava::core::json::string_field(entry.data_json, "name").value_or("");
  bool const success = bool_field_is_true(entry, "success");
  auto const external_component = ava::diagnostics::external_tool_component(name);

  std::string result;
  std::string structured_result;
  auto status = ava::core::json::string_field(entry.data_json, "status");
  if (!success && external_component)
  {
    bool const canceled = ava::core::json::string_field(entry.data_json, "status").value_or("") == "canceled";
    auto const failure = canceled ? ava::diagnostics::canceled_failure(*external_component) : ava::diagnostics::external_failure(*external_component);
    auto const safe_json = ava::diagnostics::serialize_safe_failure_json(failure);
    auto const safe_message = ava::diagnostics::serialize_safe_failure_human(failure);
    status = canceled ? "canceled" : "error";
    result = safe_json;
    structured_result = "{\"schema_version\":1,\"call_id\":" + json_string(call_id) + ",\"tool\":" + json_string(name) + ",\"status\":" + json_string(*status) +
                        ",\"ok\":false,\"summary\":" + json_string(safe_message) + ",\"content_type\":\"application/json\",\"content\":" + safe_json +
                        ",\"error\":{\"category\":" + json_string(ava::diagnostics::to_string(failure.category)) +
                        ",\"code\":" + json_string(ava::diagnostics::to_string(failure.code)) + ",\"message\":" + json_string(safe_message) +
                        "},\"truncated\":false}";
  }
  else
  {
    result = ava::core::json::string_field(entry.data_json, "result").value_or("");
    structured_result = ava::core::json::object_field(entry.data_json, "structured_result").value_or("");
  }

  std::string data = "{\"call_id\":" + json_string(call_id) + ",\"name\":" + json_string(name);
  if (preserve_output_binding)
  {
    if (auto const binding = ava::core::json::string_field(entry.data_json, "assistant_output_entry_id"); binding && !binding->empty())
      data += ",\"assistant_output_entry_id\":" + json_string(*binding);
  }
  data += ",\"success\":" + std::string(success ? "true" : "false") + ",\"result\":" + json_string(result);
  if (status)
    data += ",\"status\":" + json_string(*status);
  if (!structured_result.empty())
    data += ",\"structured_result\":" + structured_result;
  data += '}';
  return data;
}

SessionEntry sanitize_session_error_for_public_projection(SessionEntry entry)
{
  if (entry.type != EntryType::Error)
    return entry;
  entry.data_json =
      "{\"category\":\"unknown\",\"message\":" + json_string(kPublicSessionErrorOmission) + ",\"details\":" + json_string(kPublicSessionErrorOmission) + "}";
  return entry;
}

SessionEntry sanitize_session_entry_for_portable_jsonl_export(SessionEntry entry)
{
  if (entry.type == EntryType::Error)
    return sanitize_session_error_for_public_projection(std::move(entry));
  if (entry.type == EntryType::UserMessage)
    entry = sanitize_user_message_attachments_for_portable_jsonl_export(std::move(entry));
  if (entry.type == EntryType::PermissionDecision)
    entry = sanitize_permission_decision_for_portable_jsonl_export(std::move(entry));
  if (entry.type == EntryType::ToolResult)
  {
    entry.data_json = sanitized_tool_result_data_json(entry, entry.version >= 4);
    return entry;
  }
  if (entry.type != EntryType::ReasoningBlock)
    return entry;

  bool const has_native_item = ava::core::json::field_value_start(entry.data_json, "native_item_json").has_value();
  bool const has_signature = ava::core::json::field_value_start(entry.data_json, "signature").has_value();
  bool const has_redacted_data = ava::core::json::field_value_start(entry.data_json, "redacted_data").has_value();
  bool const redacted = bool_field_is_true(entry, "redacted");
  auto const provider = ava::core::json::string_field(entry.data_json, "provider").value_or("");
  auto const model = ava::core::json::string_field(entry.data_json, "model").value_or("");
  auto const format = ava::core::json::string_field(entry.data_json, "format").value_or("");
  auto text = ava::core::json::string_field(entry.data_json, "text").value_or("");
  if (redacted || text.empty())
    text = "[Provider-private reasoning metadata omitted from portable export.]";

  entry.data_json = "{\"provider\":" + json_string(provider) + ",\"model\":" + json_string(model) + ",\"format\":" + json_string(format) +
                    ",\"text\":" + json_string(text) + ",\"redacted\":" + (redacted ? "true" : "false") +
                    ",\"private_replay_metadata_omitted\":{\"native_item_json\":" + (has_native_item ? "true" : "false") +
                    ",\"signature\":" + (has_signature ? "true" : "false") + ",\"redacted_data\":" + (has_redacted_data ? "true" : "false") + "}}";
  return entry;
}

}  // namespace ava::session
