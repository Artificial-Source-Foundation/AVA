#include "sys.h"
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
