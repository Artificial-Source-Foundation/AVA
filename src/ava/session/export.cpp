#include "ava/session/export.h"

#include <string_view>

#include "ava/session/export_markdown_support.h"

namespace ava::session {
namespace {

void append_user_message(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "User");
  detail::append_export_metadata(out, entry, options);
  detail::append_export_fenced_block(out, "Message", detail::export_string_field(entry, "text").value_or(""));
}

void append_assistant_message(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "Assistant");
  detail::append_export_metadata(out, entry, options);
  detail::append_export_fenced_block(out, "Message", detail::export_string_field(entry, "text").value_or(""));
  detail::append_optional_export_fenced_block(out, "Usage", detail::export_object_field(entry, "usage"), "json");
}

void append_tool_call(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "Tool Call");
  detail::append_export_metadata(out, entry, options);
  detail::append_export_fenced_block(out, "Name", detail::export_string_field(entry, "name").value_or(""));
  detail::append_export_fenced_block(out, "Call ID", detail::export_string_field(entry, "call_id").value_or(""));
  detail::append_export_fenced_block(out, "Arguments", detail::export_string_field(entry, "arguments").value_or(""),
                                     "json");
}

void append_tool_result(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "Tool Result");
  detail::append_export_metadata(out, entry, options);
  detail::append_export_fenced_block(out, "Name", detail::export_string_field(entry, "name").value_or(""));
  detail::append_export_fenced_block(out, "Call ID", detail::export_string_field(entry, "call_id").value_or(""));
  detail::append_export_fenced_block(out, "Success", detail::export_success_text(entry));
  detail::append_export_fenced_block(out, "Result", detail::export_string_field(entry, "result").value_or(""));
}

void append_mode_change(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "Mode Change");
  detail::append_export_metadata(out, entry, options);
  detail::append_export_fenced_block(out, "Mode", detail::export_string_field(entry, "mode").value_or(""));
}

void append_model_change(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "Model Change");
  detail::append_export_metadata(out, entry, options);
  detail::append_optional_export_fenced_block(out, "Previous provider",
                                              detail::export_string_field(entry, "previous_provider"));
  detail::append_optional_export_fenced_block(out, "Previous model",
                                              detail::export_string_field(entry, "previous_model"));
  detail::append_optional_export_fenced_block(out, "Provider", detail::export_string_field(entry, "provider"));
  detail::append_optional_export_fenced_block(out, "Model", detail::export_string_field(entry, "model"));
  detail::append_optional_export_fenced_block(out, "Display name", detail::export_string_field(entry, "display_name"));
}

void append_reasoning_block(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "Reasoning");
  detail::append_export_metadata(out, entry, options);
  detail::append_optional_export_fenced_block(out, "Provider", detail::export_string_field(entry, "provider"));
  detail::append_optional_export_fenced_block(out, "Model", detail::export_string_field(entry, "model"));
  detail::append_optional_export_fenced_block(out, "Format", detail::export_string_field(entry, "format"));
  bool const redacted = detail::export_bool_field_is_true(entry, "redacted");
  detail::append_export_fenced_block(out, "Redacted", redacted ? "true" : "false");
  detail::append_export_fenced_block(out, "Signature present",
                                     detail::export_string_field(entry, "signature").has_value() ? "true" : "false");
  if (!redacted) detail::append_optional_export_fenced_block(out, "Text", detail::export_string_field(entry, "text"));
}

void append_reasoning_change(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "Reasoning Change");
  detail::append_export_metadata(out, entry, options);
  detail::append_optional_export_fenced_block(out, "Provider", detail::export_string_field(entry, "provider"));
  detail::append_optional_export_fenced_block(out, "Model", detail::export_string_field(entry, "model"));
  detail::append_optional_export_fenced_block(out, "Format", detail::export_string_field(entry, "format"));
  detail::append_optional_export_fenced_block(out, "Level", detail::export_string_field(entry, "level"));
  detail::append_optional_export_fenced_block(out, "Display", detail::export_string_field(entry, "display"));
}

void append_session_start(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "Session Start");
  detail::append_export_metadata(out, entry, options);
  detail::append_optional_export_fenced_block(out, "Mode", detail::export_string_field(entry, "mode"));
  detail::append_optional_export_fenced_block(out, "Provider", detail::export_string_field(entry, "provider"));
  detail::append_optional_export_fenced_block(out, "Model", detail::export_string_field(entry, "model"));
}

void append_compaction_number(std::string& out, SessionEntry const& entry, std::string_view label, std::string_view key)
{
  if (auto const value = detail::export_integer_field(entry, key)) {
    detail::append_export_fenced_block(out, label, std::to_string(*value));
  }
}

void append_compaction(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "Compaction");
  detail::append_export_metadata(out, entry, options);
  auto const summary = detail::export_string_field(entry, "summary")
                           .value_or("Prior context was compacted, but the summary is unavailable.");
  detail::append_export_fenced_block(out, "Summary", summary);
  detail::append_optional_export_fenced_block(out, "Carry-forward instructions",
                                              detail::export_string_field(entry, "instructions"));
  if (options.include_metadata) {
    detail::append_optional_export_fenced_block(out, "Trigger", detail::export_string_field(entry, "trigger"));
    detail::append_optional_export_fenced_block(out, "Status", detail::export_string_field(entry, "status"));
    detail::append_optional_export_fenced_block(out, "Model", detail::export_string_field(entry, "model"));
    detail::append_export_fenced_block(
        out, "Summary unavailable", detail::export_bool_field_is_true(entry, "summary_unavailable") ? "true" : "false");
    append_compaction_number(out, entry, "Estimated tokens", "estimated_tokens");
    append_compaction_number(out, entry, "Threshold tokens", "threshold_tokens");
    append_compaction_number(out, entry, "Keep recent tokens", "keep_recent_tokens");
    append_compaction_number(out, entry, "Keep recent messages", "keep_recent_messages");
  }
}

void append_error(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "Error");
  detail::append_export_metadata(out, entry, options);
  detail::append_optional_export_fenced_block(out, "Category", detail::export_string_field(entry, "category"));
  detail::append_export_fenced_block(out, "Message", detail::export_string_field(entry, "message").value_or(""));
  detail::append_optional_export_fenced_block(out, "Details", detail::export_string_field(entry, "details"));
}

void append_permission_decision(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "Permission Decision");
  detail::append_export_metadata(out, entry, options);
  detail::append_export_fenced_block(out, "Data", entry.data_json, "json");
}

void append_cancel(std::string& out, SessionEntry const& entry, ExportOptions const& options)
{
  detail::append_export_heading(out, "Cancel");
  detail::append_export_metadata(out, entry, options);
  detail::append_export_fenced_block(out, "Data", entry.data_json, "json");
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
