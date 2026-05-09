#include "ava/agent/tool_result.h"
#include "ava/core/json.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace ava::agent {
namespace {

bool has_json_value_terminator(std::string_view object, std::size_t offset)
{
  return offset >= object.size() || object[offset] == ',' || object[offset] == '}' || object[offset] == ']' || object[offset] == ' ' ||
         object[offset] == '\t' || object[offset] == '\n' || object[offset] == '\r';
}

bool bool_field_is_true(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  return start && object.substr(*start, 4) == "true" && has_json_value_terminator(object, *start + 4);
}

std::optional<std::size_t> optional_size_field(std::string_view object, std::string_view key)
{
  auto const value = ava::core::json::integer_field(object, key);
  if (!value || *value < 0)
    return std::nullopt;
  return static_cast<std::size_t>(*value);
}

void add_changed_path(ToolResultPayload& payload, std::string path)
{
  if (path.empty())
    return;
  if (std::ranges::find(payload.changed_paths, path) == payload.changed_paths.end())
  {
    payload.changed_paths.push_back(std::move(path));
  }
}

void add_permission_request_id(ToolResultPayload& payload, std::string id)
{
  if (id.empty())
    return;
  if (std::ranges::find(payload.permission_request_ids, id) == payload.permission_request_ids.end())
  {
    payload.permission_request_ids.push_back(std::move(id));
  }
}

void assign_size_field(std::optional<std::size_t>& target, std::string_view object, std::string_view key)
{
  if (auto value = optional_size_field(object, key))
    target = *value;
}

bool has_error_fields(ToolResultPayload const& payload)
{
  return !payload.error_category.empty() || !payload.error_code.empty() || !payload.error_message.empty() || !payload.error_details.empty();
}

bool path_field_represents_mutation(std::string_view tool_name)
{
  return tool_name == "write_file" || tool_name == "edit_file" || tool_name == "apply_patch";
}

void append_string_field(std::string& out, std::string_view key, std::string_view value)
{
  if (value.empty())
    return;
  out += ",\"";
  out += key;
  out += "\":\"";
  out += ava::core::json::escape(value);
  out += '"';
}

void append_required_string_field(std::string& out, std::string_view key, std::string_view value)
{
  out += ",\"";
  out += key;
  out += "\":\"";
  out += ava::core::json::escape(value);
  out += '"';
}

void append_bool_field(std::string& out, std::string_view key, bool value)
{
  out += ",\"";
  out += key;
  out += "\":";
  out += value ? "true" : "false";
}

void append_optional_number_field(std::string& out, std::string_view key, std::optional<std::size_t> const& value)
{
  if (!value)
    return;
  out += ",\"";
  out += key;
  out += "\":";
  out += std::to_string(*value);
}

void append_string_array_field(std::string& out, std::string_view key, std::vector<std::string> const& values)
{
  if (values.empty())
    return;
  out += ",\"";
  out += key;
  out += "\":[";
  for (std::size_t index = 0; index < values.size(); ++index)
  {
    if (index > 0)
      out += ',';
    out += '"';
    out += ava::core::json::escape(values[index]);
    out += '"';
  }
  out += ']';
}

void append_content_field(std::string& out, ToolResultPayload const& payload)
{
  out += ",\"content\":";
  if (payload.content_type == "application/json" && ava::core::json::is_valid_object(payload.content))
  {
    out += payload.content;
    return;
  }
  out += '"';
  out += ava::core::json::escape(payload.content);
  out += '"';
}

ToolResultPayload merge_payload_defaults(ToolResultPayload payload, std::string_view tool_name, bool success, std::string_view result_text)
{
  auto const explicit_status = payload.status;
  auto parsed = parse_tool_result_payload(tool_name, success, result_text);
  if (payload.summary.empty())
    payload.summary = std::move(parsed.summary);
  if (payload.content.empty())
    payload.content = std::move(parsed.content);
  if (payload.content_type.empty())
    payload.content_type = std::move(parsed.content_type);
  if (payload.error_category.empty())
    payload.error_category = std::move(parsed.error_category);
  if (payload.error_code.empty())
    payload.error_code = std::move(parsed.error_code);
  if (payload.error_message.empty())
    payload.error_message = std::move(parsed.error_message);
  if (payload.error_details.empty())
    payload.error_details = std::move(parsed.error_details);
  if (payload.diff.empty())
    payload.diff = std::move(parsed.diff);
  if (payload.changed_paths.empty())
    payload.changed_paths = std::move(parsed.changed_paths);
  if (payload.permission_request_ids.empty())
  {
    payload.permission_request_ids = std::move(parsed.permission_request_ids);
  }
  payload.diff_truncated = payload.diff_truncated || parsed.diff_truncated;
  payload.truncated = payload.truncated || parsed.truncated;
  payload.byte_limited = payload.byte_limited || parsed.byte_limited;
  payload.line_limited = payload.line_limited || parsed.line_limited;
  if (!payload.output_bytes)
    payload.output_bytes = parsed.output_bytes;
  if (!payload.total_bytes)
    payload.total_bytes = parsed.total_bytes;
  if (!payload.output_lines)
    payload.output_lines = parsed.output_lines;
  if (!payload.total_lines)
    payload.total_lines = parsed.total_lines;
  if (!payload.start_line)
    payload.start_line = parsed.start_line;
  if (!payload.end_line)
    payload.end_line = parsed.end_line;
  if (!payload.next_offset_line)
    payload.next_offset_line = parsed.next_offset_line;
  if (!payload.omitted_bytes)
    payload.omitted_bytes = parsed.omitted_bytes;
  if (!payload.omitted_lines)
    payload.omitted_lines = parsed.omitted_lines;
  if (!payload.visible_matches)
    payload.visible_matches = parsed.visible_matches;
  if (!payload.total_matches)
    payload.total_matches = parsed.total_matches;
  if (payload.spill_path.empty())
    payload.spill_path = std::move(parsed.spill_path);
  payload.spill_truncated = payload.spill_truncated || parsed.spill_truncated;
  payload.status = explicit_status == ToolResultStatus::Canceled ? explicit_status : parsed.status;
  return payload;
}

}  // namespace

std::string_view to_string(ToolResultStatus status) noexcept
{
  switch (status)
  {
    case ToolResultStatus::Success:
      return "success";
    case ToolResultStatus::Error:
      return "error";
    case ToolResultStatus::Canceled:
      return "canceled";
  }
  return "error";
}

ToolResultPayload parse_tool_result_payload(std::string_view tool_name, bool success, std::string_view result_text)
{
  ToolResultPayload payload;
  payload.status = success ? ToolResultStatus::Success : ToolResultStatus::Error;
  if (bool_field_is_true(result_text, "canceled"))
    payload.status = ToolResultStatus::Canceled;
  payload.content = std::string(result_text);
  payload.content_type = ava::core::json::is_valid_object(result_text) ? "application/json" : "text/plain";
  if (path_field_represents_mutation(tool_name))
  {
    add_changed_path(payload, ava::core::json::string_field(result_text, "path").value_or(""));
  }
  for (auto const& path : ava::core::json::strings_in_array_field(result_text, "changed_paths"))
  {
    add_changed_path(payload, path);
  }
  for (auto const& path : ava::core::json::strings_in_array_field(result_text, "changed_files"))
  {
    add_changed_path(payload, path);
  }
  for (auto const& edit : ava::core::json::objects_in_array_field(result_text, "edits"))
  {
    add_changed_path(payload, ava::core::json::string_field(edit, "path").value_or(""));
  }
  add_permission_request_id(payload, ava::core::json::string_field(result_text, "permission_request_id").value_or(""));
  for (auto const& id : ava::core::json::strings_in_array_field(result_text, "permission_request_ids"))
  {
    add_permission_request_id(payload, id);
  }

  payload.diff = ava::core::json::string_field(result_text, "diff").value_or("");
  payload.diff_truncated = bool_field_is_true(result_text, "diff_truncated");
  payload.truncated = bool_field_is_true(result_text, "truncated");
  payload.byte_limited = bool_field_is_true(result_text, "byte_limited");
  payload.line_limited = bool_field_is_true(result_text, "line_limited");
  payload.spill_truncated = bool_field_is_true(result_text, "spill_truncated");
  payload.spill_path = ava::core::json::string_field(result_text, "spill_path").value_or(ava::core::json::string_field(result_text, "spill_file").value_or(""));
  assign_size_field(payload.output_bytes, result_text, "output_bytes");
  assign_size_field(payload.total_bytes, result_text, "total_bytes");
  assign_size_field(payload.output_lines, result_text, "output_lines");
  assign_size_field(payload.output_lines, result_text, "visible_lines");
  assign_size_field(payload.output_lines, result_text, "returned_lines");
  assign_size_field(payload.total_lines, result_text, "total_lines");
  assign_size_field(payload.start_line, result_text, "start_line");
  assign_size_field(payload.end_line, result_text, "end_line");
  assign_size_field(payload.next_offset_line, result_text, "next_offset_line");
  assign_size_field(payload.next_offset_line, result_text, "next_offset");
  assign_size_field(payload.omitted_bytes, result_text, "omitted_bytes");
  assign_size_field(payload.omitted_bytes, result_text, "omitted_output_bytes");
  assign_size_field(payload.omitted_lines, result_text, "omitted_lines");
  assign_size_field(payload.omitted_lines, result_text, "omitted_line_count");
  assign_size_field(payload.visible_matches, result_text, "visible_matches");
  assign_size_field(payload.visible_matches, result_text, "output_matches");
  assign_size_field(payload.visible_matches, result_text, "returned_matches");
  assign_size_field(payload.total_matches, result_text, "total_matches");

  if (auto const error = ava::core::json::object_field(result_text, "error"))
  {
    payload.error_category = ava::core::json::string_field(*error, "category").value_or("");
    payload.error_code = ava::core::json::string_field(*error, "code").value_or("");
    payload.error_message = ava::core::json::string_field(*error, "message").value_or("");
    payload.error_details = ava::core::json::string_field(*error, "details").value_or("");
  }
  if (payload.error_message.empty() && !success)
    payload.error_message = std::string(tool_name) + " failed";
  return payload;
}

ToolDispatchResult with_tool_result_payload(ToolDispatchResult result)
{
  result.payload = merge_payload_defaults(std::move(result.payload), result.name, result.success, result.result_text);
  return result;
}

std::string serialize_tool_result_payload_json(ToolDispatchResult const& result)
{
  auto const payload = merge_payload_defaults(result.payload, result.name, result.success, result.result_text);
  std::string out = "{\"schema_version\":1";
  append_required_string_field(out, "call_id", result.call_id);
  append_required_string_field(out, "tool", result.name);
  append_required_string_field(out, "status", to_string(payload.status));
  append_bool_field(out, "ok", result.success);
  append_string_field(out, "summary", payload.summary);
  append_required_string_field(out, "content_type", payload.content_type.empty() ? "text/plain" : payload.content_type);
  append_content_field(out, payload);
  if (has_error_fields(payload))
  {
    out += ",\"error\":{";
    bool needs_comma = false;
    auto const append_error_string = [&](std::string_view key, std::string_view value) {
      if (value.empty())
        return;
      if (needs_comma)
        out += ',';
      out += '"';
      out += key;
      out += "\":\"";
      out += ava::core::json::escape(value);
      out += '"';
      needs_comma = true;
    };
    append_error_string("category", payload.error_category);
    append_error_string("code", payload.error_code);
    append_error_string("message", payload.error_message);
    append_error_string("details", payload.error_details);
    out += '}';
  }
  append_string_field(out, "diff", payload.diff);
  if (!payload.diff.empty() || payload.diff_truncated)
    append_bool_field(out, "diff_truncated", payload.diff_truncated);
  append_string_array_field(out, "changed_paths", payload.changed_paths);
  append_string_array_field(out, "permission_request_ids", payload.permission_request_ids);
  append_bool_field(out, "truncated", payload.truncated);
  if (payload.byte_limited)
    append_bool_field(out, "byte_limited", payload.byte_limited);
  if (payload.line_limited)
    append_bool_field(out, "line_limited", payload.line_limited);
  append_optional_number_field(out, "output_bytes", payload.output_bytes);
  append_optional_number_field(out, "total_bytes", payload.total_bytes);
  append_optional_number_field(out, "output_lines", payload.output_lines);
  append_optional_number_field(out, "total_lines", payload.total_lines);
  append_optional_number_field(out, "start_line", payload.start_line);
  append_optional_number_field(out, "end_line", payload.end_line);
  append_optional_number_field(out, "next_offset_line", payload.next_offset_line);
  append_optional_number_field(out, "omitted_bytes", payload.omitted_bytes);
  append_optional_number_field(out, "omitted_lines", payload.omitted_lines);
  append_optional_number_field(out, "visible_matches", payload.visible_matches);
  append_optional_number_field(out, "total_matches", payload.total_matches);
  append_string_field(out, "spill_path", payload.spill_path);
  if (!payload.spill_path.empty() || payload.spill_truncated)
  {
    append_bool_field(out, "spill_truncated", payload.spill_truncated);
  }
  out += '}';
  return out;
}

}  // namespace ava::agent
