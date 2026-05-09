#include "ava/agent/tool_summaries.h"
#include "ava/core/json.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ava::agent {
namespace {

constexpr std::size_t kMaxSummaryValueBytes = 80;
constexpr std::size_t kMaxToolSummaryBytes = 180;

bool bool_field_is_true(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  return start && object.substr(*start, 4) == "true";
}

std::string safe_summary_text(std::string text, std::size_t max_bytes = kMaxSummaryValueBytes)
{
  for (char& ch : text)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F)
      ch = '?';
  }
  if (text.size() <= max_bytes)
    return text;
  constexpr std::string_view marker = "...";
  if (max_bytes <= marker.size())
  {
    text.resize(max_bytes);
    return text;
  }
  text.resize(max_bytes - marker.size());
  text += marker;
  return text;
}

std::string size_summary(std::string_view name, std::size_t bytes)
{
  return std::string(name) + "=" + std::to_string(bytes) + " bytes";
}

std::string append_summary_part(std::string summary, std::string part)
{
  if (part.empty())
    return summary;
  if (!summary.empty())
    summary += ", ";
  summary += std::move(part);
  return safe_summary_text(std::move(summary), kMaxToolSummaryBytes);
}

std::string string_arg_summary(std::string_view arguments, std::string_view field)
{
  auto const value = ava::core::json::string_field(arguments, field);
  if (!value)
    return {};
  return std::string(field) + "=" + safe_summary_text(*value);
}

std::optional<std::size_t> size_field(std::string_view object, std::string_view key)
{
  auto const value = ava::core::json::integer_field(object, key);
  if (!value || *value < 0)
    return std::nullopt;
  return static_cast<std::size_t>(*value);
}

std::string line_range_summary(std::size_t start_line, std::size_t end_line, std::size_t total_lines)
{
  if (start_line == 0 || end_line == 0)
    return "0/" + std::to_string(total_lines) + " lines";
  return "lines " + std::to_string(start_line) + "-" + std::to_string(end_line) + "/" + std::to_string(total_lines);
}

std::string summarize_tool_error(std::string_view result_text)
{
  auto const error = ava::core::json::object_field(result_text, "error");
  if (!error)
    return "error";
  auto const message = ava::core::json::string_field(*error, "message");
  if (!message || message->empty())
    return "error";
  return "error: " + safe_summary_text(*message);
}

}  // namespace

std::string summarize_tool_arguments(ProviderToolCall const& call)
{
  auto const arguments = call.arguments_json.empty() ? std::string_view("{}") : std::string_view(call.arguments_json);
  std::string summary;
  if (call.name == "read_file")
  {
    summary = append_summary_part(std::move(summary), string_arg_summary(arguments, "path"));
    if (auto const offset = ava::core::json::integer_field(arguments, "offset"))
    {
      summary = append_summary_part(std::move(summary), "offset=" + std::to_string(*offset));
    }
    if (auto const limit = ava::core::json::integer_field(arguments, "limit"))
    {
      summary = append_summary_part(std::move(summary), "limit=" + std::to_string(*limit));
    }
    if (auto const max_bytes = ava::core::json::integer_field(arguments, "max_bytes"))
    {
      summary = append_summary_part(std::move(summary), "max_bytes=" + std::to_string(*max_bytes));
    }
    return summary;
  }
  if (call.name == "write_file")
  {
    summary = append_summary_part(std::move(summary), string_arg_summary(arguments, "path"));
    if (auto const content = ava::core::json::string_field(arguments, "content"))
    {
      summary = append_summary_part(std::move(summary), size_summary("content", content->size()));
    }
    return summary;
  }
  if (call.name == "edit_file")
  {
    summary = append_summary_part(std::move(summary), string_arg_summary(arguments, "path"));
    if (auto const old_text = ava::core::json::string_field(arguments, "old_text"))
    {
      summary = append_summary_part(std::move(summary), size_summary("old_text", old_text->size()));
    }
    if (auto const new_text = ava::core::json::string_field(arguments, "new_text"))
    {
      summary = append_summary_part(std::move(summary), size_summary("new_text", new_text->size()));
    }
    return summary;
  }
  if (call.name == "glob")
  {
    summary = append_summary_part(std::move(summary), string_arg_summary(arguments, "pattern"));
    if (auto const max_results = ava::core::json::integer_field(arguments, "max_results"))
    {
      summary = append_summary_part(std::move(summary), "max_results=" + std::to_string(*max_results));
    }
    return summary;
  }
  if (call.name == "grep")
  {
    summary = append_summary_part(std::move(summary), string_arg_summary(arguments, "pattern"));
    summary = append_summary_part(std::move(summary), string_arg_summary(arguments, "include"));
    return summary;
  }
  if (call.name == "bash")
  {
    summary = append_summary_part(std::move(summary), string_arg_summary(arguments, "command"));
    if (auto const max_lines = ava::core::json::integer_field(arguments, "max_lines"))
    {
      summary = append_summary_part(std::move(summary), "max_lines=" + std::to_string(*max_lines));
    }
    if (auto const limit = ava::core::json::integer_field(arguments, "limit"))
    {
      summary = append_summary_part(std::move(summary), "limit=" + std::to_string(*limit));
    }
    return summary;
  }
  if (call.name == "webfetch")
  {
    summary = append_summary_part(std::move(summary), string_arg_summary(arguments, "url"));
    if (auto const offset = ava::core::json::integer_field(arguments, "offset"))
    {
      summary = append_summary_part(std::move(summary), "offset=" + std::to_string(*offset));
    }
    if (auto const limit = ava::core::json::integer_field(arguments, "limit"))
    {
      summary = append_summary_part(std::move(summary), "limit=" + std::to_string(*limit));
    }
    return summary;
  }
  if (call.name == "apply_patch")
  {
    auto const edits = ava::core::json::objects_in_array_field(arguments, "edits");
    summary = "edits=" + std::to_string(edits.size());
    if (!edits.empty())
      summary = append_summary_part(std::move(summary), string_arg_summary(edits.front(), "path"));
    return summary;
  }
  if (call.name == "question")
    return string_arg_summary(arguments, "question");
  return call.arguments_json.empty() ? std::string{} : "arguments provided";
}

std::string summarize_tool_result(ToolDispatchResult const& result)
{
  if (!result.payload.summary.empty())
    return result.payload.summary;
  auto const& payload = result.payload;
  if (!result.success && !payload.error_message.empty())
    return "error: " + safe_summary_text(payload.error_message);
  if (!result.success)
    return summarize_tool_error(result.result_text);
  if (result.name == "read_file")
  {
    auto const output_lines = payload.output_lines.value_or(size_field(result.result_text, "output_lines").value_or(0));
    auto const total_lines = payload.total_lines.value_or(size_field(result.result_text, "total_lines").value_or(0));
    if (total_lines > 0 || output_lines > 0)
    {
      auto const start_line = payload.start_line.value_or(size_field(result.result_text, "start_line").value_or(0));
      auto const end_line = payload.end_line.value_or(size_field(result.result_text, "end_line").value_or(0));
      auto const next_offset = payload.next_offset_line.value_or(
          size_field(result.result_text, "next_offset_line").value_or(size_field(result.result_text, "next_offset").value_or(0)));
      std::string summary = line_range_summary(start_line, end_line, total_lines);
      if (payload.truncated || bool_field_is_true(result.result_text, "truncated"))
        summary += " (truncated)";
      if (next_offset > 0)
        summary += " (next offset " + std::to_string(next_offset) + ")";
      return summary;
    }
    auto const output_bytes = payload.output_bytes.value_or(size_field(result.result_text, "output_bytes").value_or(0));
    auto const total_bytes = payload.total_bytes.value_or(size_field(result.result_text, "total_bytes").value_or(output_bytes));
    std::string summary = "read " + std::to_string(output_bytes) + "/" + std::to_string(total_bytes) + " bytes";
    if (payload.truncated || bool_field_is_true(result.result_text, "truncated"))
      summary += " (truncated)";
    return summary;
  }
  if (result.name == "write_file" || result.name == "edit_file")
  {
    auto const bytes = ava::core::json::integer_field(result.result_text, "bytes_written").value_or(0);
    return "wrote " + std::to_string(bytes) + " bytes";
  }
  if (result.name == "glob")
  {
    auto const total = payload.total_matches.value_or(ava::core::json::integer_field(result.result_text, "total_matches").value_or(0));
    std::string summary = std::to_string(total) + " matches";
    if (payload.truncated || bool_field_is_true(result.result_text, "truncated"))
      summary += " (truncated)";
    return summary;
  }
  if (result.name == "grep")
  {
    auto const total = payload.total_matches.value_or(ava::core::json::integer_field(result.result_text, "total_matches").value_or(0));
    std::string summary = std::to_string(total) + " matches";
    if (payload.truncated || bool_field_is_true(result.result_text, "truncated"))
      summary += " (truncated)";
    return summary;
  }
  if (result.name == "bash")
  {
    auto const exit_code = ava::core::json::integer_field(result.result_text, "exit_code").value_or(0);
    std::string summary = "exit " + std::to_string(exit_code);
    if (bool_field_is_true(result.result_text, "timed_out"))
      summary += " (timed out)";
    if (bool_field_is_true(result.result_text, "canceled"))
      summary += " (canceled)";
    if (bool_field_is_true(result.result_text, "truncated"))
    {
      auto const output_lines = payload.output_lines.value_or(size_field(result.result_text, "output_lines").value_or(0));
      auto const total_lines = payload.total_lines.value_or(size_field(result.result_text, "total_lines").value_or(0));
      if (total_lines > 0 || output_lines > 0)
      {
        summary += " (output truncated to " + std::to_string(output_lines) + "/" + std::to_string(total_lines) + " lines)";
      }
      else
      {
        summary += " (output truncated)";
      }
    }
    return summary;
  }
  if (result.name == "webfetch")
  {
    auto const output_lines = payload.output_lines.value_or(size_field(result.result_text, "output_lines").value_or(0));
    auto const total_lines = payload.total_lines.value_or(size_field(result.result_text, "total_lines").value_or(0));
    if (total_lines > 0 || output_lines > 0)
    {
      auto const start_line = payload.start_line.value_or(size_field(result.result_text, "start_line").value_or(0));
      auto const end_line = payload.end_line.value_or(size_field(result.result_text, "end_line").value_or(0));
      std::string summary = "fetched " + line_range_summary(start_line, end_line, total_lines);
      if (payload.truncated || bool_field_is_true(result.result_text, "truncated"))
        summary += " (truncated)";
      return summary;
    }
    return "fetched";
  }
  if (result.name == "apply_patch")
  {
    auto const edits = ava::core::json::objects_in_array_field(result.result_text, "edits");
    return "applied " + std::to_string(edits.size()) + " edits";
  }
  if (result.name == "question")
    return "question recorded";
  return "ok";
}

}  // namespace ava::agent
