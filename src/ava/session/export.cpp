#include "ava/session/export.h"

#include "ava/core/json.h"

#include <algorithm>
#include <optional>
#include <string_view>

namespace ava::session {
namespace {

std::string json_string(std::string_view value)
{
  return "\"" + ava::core::json::escape(value) + "\"";
}

std::size_t longest_backtick_run(std::string_view text) noexcept
{
  std::size_t longest = 0;
  std::size_t current = 0;
  for (char const ch : text) {
    if (ch == '`') {
      ++current;
      longest = std::max(longest, current);
    } else {
      current = 0;
    }
  }
  return longest;
}

std::string fence_for(std::string_view text)
{
  return std::string(std::max<std::size_t>(3, longest_backtick_run(text) + 1), '`');
}

std::string sanitize_fenced_content(std::string_view content)
{
  std::string sanitized;
  sanitized.reserve(content.size());
  constexpr char kHex[] = "0123456789ABCDEF";
  for (char const ch : content) {
    auto const byte = static_cast<unsigned char>(ch);
    if ((byte < 0x20 && ch != '\n' && ch != '\t') || byte == 0x7F) {
      sanitized += "\\u00";
      sanitized.push_back(kHex[(byte >> 4U) & 0x0FU]);
      sanitized.push_back(kHex[byte & 0x0FU]);
    } else {
      sanitized.push_back(ch);
    }
  }
  return sanitized;
}

void append_heading(std::string& out, std::string_view heading)
{
  out += "## ";
  out += heading;
  out += "\n\n";
}

void append_fenced_block(std::string& out, std::string_view label, std::string_view content,
                         std::string_view language = "text")
{
  out += label;
  out += ":\n\n";
  auto const sanitized_content = sanitize_fenced_content(content);
  auto const fence = fence_for(sanitized_content);
  out += fence;
  if (!language.empty()) out += language;
  out += '\n';
  out += sanitized_content;
  if (!sanitized_content.empty() && sanitized_content.back() != '\n') out += '\n';
  out += fence;
  out += "\n\n";
}

void append_optional_fenced_block(std::string& out, std::string_view label, std::optional<std::string> const& content,
                                  std::string_view language = "text")
{
  if (content && !content->empty()) append_fenced_block(out, label, *content, language);
}

std::string metadata_json(SessionEntry const& entry)
{
  return "{\"id\":" + json_string(entry.id) + ",\"parent_id\":" + json_string(entry.parent_id) +
         ",\"type\":" + json_string(to_string(entry.type)) + ",\"timestamp\":" + json_string(entry.timestamp) + "}";
}

void append_metadata(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  if (!options.include_metadata) return;
  append_fenced_block(out, "Metadata", metadata_json(entry), "json");
}

std::optional<std::string> string_field(SessionEntry const& entry, std::string_view key)
{
  return ava::core::json::string_field(entry.data_json, key);
}

std::optional<long long> integer_field(SessionEntry const& entry, std::string_view key)
{
  return ava::core::json::integer_field(entry.data_json, key);
}

std::optional<std::string> object_field(SessionEntry const& entry, std::string_view key)
{
  return ava::core::json::object_field(entry.data_json, key);
}

bool bool_field_is_true(SessionEntry const& entry, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(entry.data_json, key);
  return start && entry.data_json.substr(*start, 4) == "true";
}

std::string success_text(SessionEntry const& entry)
{
  auto const start = ava::core::json::field_value_start(entry.data_json, "success");
  if (!start) return "unknown";
  if (entry.data_json.substr(*start, 4) == "true") return "true";
  if (entry.data_json.substr(*start, 5) == "false") return "false";
  return "unknown";
}

void append_user_message(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "User");
  append_metadata(out, entry, options);
  append_fenced_block(out, "Message", string_field(entry, "text").value_or(""));
}

void append_assistant_message(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "Assistant");
  append_metadata(out, entry, options);
  append_fenced_block(out, "Message", string_field(entry, "text").value_or(""));
  append_optional_fenced_block(out, "Usage", object_field(entry, "usage"), "json");
}

void append_tool_call(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "Tool Call");
  append_metadata(out, entry, options);
  append_fenced_block(out, "Name", string_field(entry, "name").value_or(""));
  append_fenced_block(out, "Call ID", string_field(entry, "call_id").value_or(""));
  append_fenced_block(out, "Arguments", string_field(entry, "arguments").value_or(""), "json");
}

void append_tool_result(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "Tool Result");
  append_metadata(out, entry, options);
  append_fenced_block(out, "Name", string_field(entry, "name").value_or(""));
  append_fenced_block(out, "Call ID", string_field(entry, "call_id").value_or(""));
  append_fenced_block(out, "Success", success_text(entry));
  append_fenced_block(out, "Result", string_field(entry, "result").value_or(""));
}

void append_mode_change(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "Mode Change");
  append_metadata(out, entry, options);
  append_fenced_block(out, "Mode", string_field(entry, "mode").value_or(""));
}

void append_model_change(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "Model Change");
  append_metadata(out, entry, options);
  append_optional_fenced_block(out, "Previous provider", string_field(entry, "previous_provider"));
  append_optional_fenced_block(out, "Previous model", string_field(entry, "previous_model"));
  append_optional_fenced_block(out, "Provider", string_field(entry, "provider"));
  append_optional_fenced_block(out, "Model", string_field(entry, "model"));
  append_optional_fenced_block(out, "Display name", string_field(entry, "display_name"));
}

void append_reasoning_block(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "Reasoning");
  append_metadata(out, entry, options);
  append_optional_fenced_block(out, "Provider", string_field(entry, "provider"));
  append_optional_fenced_block(out, "Model", string_field(entry, "model"));
  append_optional_fenced_block(out, "Format", string_field(entry, "format"));
  bool const redacted = bool_field_is_true(entry, "redacted");
  append_fenced_block(out, "Redacted", redacted ? "true" : "false");
  append_fenced_block(out, "Signature present", string_field(entry, "signature").has_value() ? "true" : "false");
  if (!redacted) append_optional_fenced_block(out, "Text", string_field(entry, "text"));
}

void append_reasoning_change(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "Reasoning Change");
  append_metadata(out, entry, options);
  append_optional_fenced_block(out, "Provider", string_field(entry, "provider"));
  append_optional_fenced_block(out, "Model", string_field(entry, "model"));
  append_optional_fenced_block(out, "Format", string_field(entry, "format"));
  append_optional_fenced_block(out, "Level", string_field(entry, "level"));
  append_optional_fenced_block(out, "Display", string_field(entry, "display"));
}

void append_session_start(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "Session Start");
  append_metadata(out, entry, options);
  append_optional_fenced_block(out, "Mode", string_field(entry, "mode"));
  append_optional_fenced_block(out, "Provider", string_field(entry, "provider"));
  append_optional_fenced_block(out, "Model", string_field(entry, "model"));
}

void append_compaction_number(std::string& out, SessionEntry const& entry, std::string_view label, std::string_view key)
{
  if (auto const value = integer_field(entry, key)) append_fenced_block(out, label, std::to_string(*value));
}

void append_compaction(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "Compaction");
  append_metadata(out, entry, options);
  auto const summary =
      string_field(entry, "summary").value_or("Prior context was compacted, but the summary is unavailable.");
  append_fenced_block(out, "Summary", summary);
  append_optional_fenced_block(out, "Carry-forward instructions", string_field(entry, "instructions"));
  if (options.include_metadata) {
    append_optional_fenced_block(out, "Trigger", string_field(entry, "trigger"));
    append_optional_fenced_block(out, "Status", string_field(entry, "status"));
    append_optional_fenced_block(out, "Model", string_field(entry, "model"));
    append_fenced_block(out, "Summary unavailable",
                        bool_field_is_true(entry, "summary_unavailable") ? "true" : "false");
    append_compaction_number(out, entry, "Estimated tokens", "estimated_tokens");
    append_compaction_number(out, entry, "Threshold tokens", "threshold_tokens");
    append_compaction_number(out, entry, "Keep recent tokens", "keep_recent_tokens");
    append_compaction_number(out, entry, "Keep recent messages", "keep_recent_messages");
  }
}

void append_error(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "Error");
  append_metadata(out, entry, options);
  append_optional_fenced_block(out, "Category", string_field(entry, "category"));
  append_fenced_block(out, "Message", string_field(entry, "message").value_or(""));
  append_optional_fenced_block(out, "Details", string_field(entry, "details"));
}

void append_permission_decision(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "Permission Decision");
  append_metadata(out, entry, options);
  append_fenced_block(out, "Data", entry.data_json, "json");
}

void append_cancel(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  append_heading(out, "Cancel");
  append_metadata(out, entry, options);
  append_fenced_block(out, "Data", entry.data_json, "json");
}

}  // namespace

std::string format_session_markdown(std::vector<SessionEntry> const& entries, ExportOptions const& options)
{
  std::string out = "# AVA Session Export\n\n";

  for (auto const& entry : entries) {
    if (is_internal_replay_user_message(entry)) continue;
    switch (entry.type) {
      case EntryType::SessionStart:
        append_session_start(out, entry, options);
        break;
      case EntryType::UserMessage:
        append_user_message(out, entry, options);
        break;
      case EntryType::AssistantMessage:
        append_assistant_message(out, entry, options);
        break;
      case EntryType::ToolCall:
        if (options.include_tool_details) append_tool_call(out, entry, options);
        break;
      case EntryType::ToolResult:
        if (options.include_tool_details) append_tool_result(out, entry, options);
        break;
      case EntryType::PermissionDecision:
        append_permission_decision(out, entry, options);
        break;
      case EntryType::ModeChange:
        append_mode_change(out, entry, options);
        break;
      case EntryType::ModelChange:
        append_model_change(out, entry, options);
        break;
      case EntryType::ReasoningBlock:
        append_reasoning_block(out, entry, options);
        break;
      case EntryType::ReasoningChange:
        append_reasoning_change(out, entry, options);
        break;
      case EntryType::Compaction:
        if (options.include_compactions) append_compaction(out, entry, options);
        break;
      case EntryType::Error:
        append_error(out, entry, options);
        break;
      case EntryType::Cancel:
        append_cancel(out, entry, options);
        break;
    }
  }

  return out;
}

}  // namespace ava::session
