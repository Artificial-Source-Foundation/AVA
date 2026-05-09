#include "ava/tui/composer_internal.h"
#include "ava/tui/tool_cards.h"
#include "ava/core/json.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>
#include <vector>

namespace ava::tui {
namespace {

constexpr auto kBlockMinWidth = std::size_t{32};
constexpr auto kCollapsedOutputPreviewLines = std::size_t{2};
constexpr auto kExpandedOutputPreviewLines = std::size_t{8};
constexpr auto kExpandedDiffPreviewLines = std::size_t{14};

bool wide_blocks(std::size_t width)
{
  return width >= kBlockMinWidth;
}

std::string lower_ascii(std::string_view text)
{
  std::string lowered;
  lowered.reserve(text.size());
  for (char const ch : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

bool bool_json_field(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  if (!start)
    return false;
  auto value = object.substr(*start);
  while (!value.empty())
  {
    auto const byte = static_cast<unsigned char>(value.front());
    if (byte != ' ' && byte != '\n' && byte != '\r' && byte != '\t')
      break;
    value.remove_prefix(1);
  }
  return value.starts_with("true");
}

std::optional<std::string> first_json_string(std::string_view object, std::initializer_list<std::string_view> keys)
{
  for (auto const key : keys)
  {
    if (auto value = ava::core::json::string_field(object, key); value && !value->empty())
      return value;
  }
  return std::nullopt;
}

std::optional<long long> first_json_integer(std::string_view object, std::initializer_list<std::string_view> keys)
{
  for (auto const key : keys)
  {
    if (auto value = ava::core::json::integer_field(object, key))
      return value;
  }
  return std::nullopt;
}

std::string format_duration_ms(long long milliseconds)
{
  if (milliseconds < 0)
    return {};
  if (milliseconds < 1000)
    return std::to_string(milliseconds) + "ms";
  if (milliseconds < 60'000)
  {
    auto const whole = milliseconds / 1000;
    auto const tenths = (milliseconds % 1000) / 100;
    return std::to_string(whole) + "." + std::to_string(tenths) + "s";
  }
  auto const minutes = milliseconds / 60'000;
  auto const seconds = (milliseconds % 60'000) / 1000;
  return std::to_string(minutes) + "m" + std::to_string(seconds) + "s";
}

std::optional<std::string> duration_text(ToolTimelineItem const& item)
{
  auto const value = first_json_integer(item.result_json, {"duration_ms", "elapsed_ms", "runtime_ms"});
  if (!value)
    return std::nullopt;
  auto formatted = format_duration_ms(*value);
  if (formatted.empty())
    return std::nullopt;
  return formatted;
}

std::optional<std::string> command_from_summary(std::string_view summary)
{
  constexpr std::string_view marker = "command=";
  auto const marker_offset = summary.find(marker);
  if (marker_offset == std::string_view::npos)
    return std::nullopt;
  auto value = summary.substr(marker_offset + marker.size());
  bool in_single_quote = false;
  bool in_double_quote = false;
  bool escaped = false;
  for (std::size_t index = 0; index + 1 < value.size(); ++index)
  {
    auto const ch = value[index];
    if (escaped)
    {
      escaped = false;
      continue;
    }
    if (ch == '\\' && !in_single_quote)
    {
      escaped = true;
      continue;
    }
    if (ch == '\'' && !in_double_quote)
    {
      in_single_quote = !in_single_quote;
      continue;
    }
    if (ch == '"' && !in_single_quote)
    {
      in_double_quote = !in_double_quote;
      continue;
    }
    if (!in_single_quote && !in_double_quote && ch == ',' && value[index + 1] == ' ')
    {
      value = value.substr(0, index);
      break;
    }
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  if (value.empty())
    return std::nullopt;
  return std::string(value);
}

std::optional<std::string> command_text(ToolTimelineItem const& item)
{
  if (auto command = first_json_string(item.arguments_json, {"command", "cmd", "shell_command"}))
    return command;
  if (auto command = first_json_string(item.result_json, {"command", "cmd", "shell_command"}))
    return command;
  if (auto command = command_from_summary(item.argument_summary))
    return command;
  auto const lowered = lower_ascii(item.name);
  if ((lowered == "bash" || lowered == "shell" || lowered == "run_command") && !item.argument_summary.empty())
  {
    return item.argument_summary;
  }
  return std::nullopt;
}

bool shell_tool(ToolTimelineItem const& item)
{
  auto const lowered_name = lower_ascii(item.name);
  if (lowered_name == "bash" || lowered_name == "shell" || lowered_name == "run_command" || lowered_name == "shell_run")
  {
    return true;
  }
  if (auto tool = first_json_string(item.arguments_json, {"tool", "type"}))
  {
    auto const lowered_tool = lower_ascii(*tool);
    if (lowered_tool == "bash" || lowered_tool == "shell")
      return true;
  }
  if (auto tool = first_json_string(item.result_json, {"tool", "type"}))
  {
    auto const lowered_tool = lower_ascii(*tool);
    if (lowered_tool == "bash" || lowered_tool == "shell")
      return true;
  }
  return command_text(item).has_value() && lowered_name.find("bash") != std::string::npos;
}

std::optional<std::string> exit_status_text(ToolTimelineItem const& item)
{
  if (item.status == ToolTimelineStatus::Running)
    return std::string("running");
  if (bool_json_field(item.result_json, "canceled"))
    return std::string("canceled");
  if (bool_json_field(item.result_json, "timed_out"))
    return std::string("timed out");
  if (auto exit_code = first_json_integer(item.result_json, {"exit_code", "status_code"}))
  {
    return "exit " + std::to_string(*exit_code);
  }
  if (!item.result_summary.empty())
    return item.result_summary;
  return to_string(item.status);
}

std::optional<std::string> output_preview_text(ToolTimelineItem const& item)
{
  if (auto output = first_json_string(item.result_json, {"output", "stdout", "stderr", "content", "preview"}))
  {
    return output;
  }
  if (item.result_summary.find('\n') != std::string::npos || item.result_summary.find('\r') != std::string::npos)
  {
    return item.result_summary;
  }
  return std::nullopt;
}

std::size_t known_total_output_lines(ToolTimelineItem const& item, std::size_t actual_lines)
{
  auto total = item.total_lines.value_or(actual_lines);
  total = std::max(total, actual_lines);
  if (item.omitted_lines)
    total = std::max(total, actual_lines + *item.omitted_lines);
  return total;
}

void append_output_preview_lines(std::vector<std::string>& lines, ToolTimelineItem const& item, std::string_view label, std::string const& text,
                                 std::size_t width, std::size_t max_visible)
{
  if (text.empty() || max_visible == 0)
    return;
  auto raw_lines = split_lines(text);
  if (raw_lines.size() == 1 && raw_lines.front().empty())
    return;

  auto const prefix = wide_blocks(width) ? std::string("  │     ") : std::string("      ");
  auto const content_prefix = wide_blocks(width) ? std::string("  │       ") : std::string("        ");
  auto const visible = std::min(raw_lines.size(), max_visible);
  auto const known_total = known_total_output_lines(item, raw_lines.size());
  auto const hidden_by_total = known_total > visible ? known_total - visible : std::size_t{0};
  auto const hidden_by_local = raw_lines.size() > visible ? raw_lines.size() - visible : std::size_t{0};
  auto const hidden_by_omitted = item.omitted_lines.value_or(0) + hidden_by_local;
  auto const hidden = std::max(hidden_by_total, hidden_by_omitted);

  std::string header = prefix + std::string(detail::kSgrDim) + std::string(label) + ": " + std::to_string(visible) + " shown";
  if (known_total > visible || hidden > 0)
    header += "/" + std::to_string(known_total);
  header += visible == 1 ? " line" : " lines";
  if (hidden > 0)
    header += " · " + std::to_string(hidden) + " hidden";
  header += std::string(detail::kSgrReset);
  lines.push_back(detail::fit_line_preserving_sgr(std::move(header), width));

  for (std::size_t index = 0; index < visible; ++index)
  {
    auto sanitized = sanitize_terminal_text(raw_lines[index]);
    lines.push_back(
        detail::fit_line_preserving_sgr(content_prefix + std::string(detail::kSgrMuted) + std::move(sanitized) + std::string(detail::kSgrReset), width));
  }
}

void append_tool_detail_lines(std::vector<std::string>& lines, std::string_view label, std::string const& text, std::size_t width)
{
  if (text.empty())
    return;
  auto const prefix = wide_blocks(width) ? std::string("  │     ") : std::string("      ");
  auto const label_prefix = prefix + std::string(detail::kSgrDim) + std::string(label) + ": " + std::string(detail::kSgrReset);
  for (auto const& raw_line : split_lines(text))
  {
    lines.push_back(detail::fit_line_preserving_sgr(label_prefix + sanitize_terminal_text(raw_line), width));
  }
}

void append_diff_lines(std::vector<std::string>& lines, ToolTimelineItem const& item, std::size_t width)
{
  if (item.diff.empty())
    return;
  auto const prefix = wide_blocks(width) ? std::string("  │     ") : std::string("      ");
  auto label = std::string("diff");
  if (!item.changed_paths.empty())
  {
    label += " ";
    for (std::size_t index = 0; index < item.changed_paths.size() && index < 3; ++index)
    {
      if (index > 0)
        label += ", ";
      label += sanitize_terminal_text(item.changed_paths[index]);
    }
    if (item.changed_paths.size() > 3)
      label += ", +" + std::to_string(item.changed_paths.size() - 3) + " more";
  }
  lines.push_back(detail::fit_line_preserving_sgr(prefix + std::string(detail::kSgrDim) + std::move(label) + ":" + std::string(detail::kSgrReset), width));
  auto const content_prefix = wide_blocks(width) ? std::string("  │       ") : std::string("        ");
  auto diff_lines = detail::render_unified_diff_body(item.diff, item.diff_truncated, width, content_prefix, kExpandedDiffPreviewLines);
  lines.insert(lines.end(), diff_lines.begin(), diff_lines.end());
}

std::string truncation_summary(ToolTimelineItem const& item)
{
  if (!item.truncated && item.spill_path.empty() && !item.spill_truncated)
    return {};

  std::string summary;
  if (item.truncated)
  {
    if (item.start_line && item.end_line && item.total_lines)
    {
      summary = "truncated lines " + std::to_string(*item.start_line) + "-" + std::to_string(*item.end_line) + "/" + std::to_string(*item.total_lines);
    }
    else if (item.output_lines && item.total_lines)
    {
      summary = "truncated " + std::to_string(*item.output_lines) + "/" + std::to_string(*item.total_lines) + " lines";
    }
    else if (item.visible_matches && item.total_matches)
    {
      summary = "truncated " + std::to_string(*item.visible_matches) + "/" + std::to_string(*item.total_matches) + " matches";
    }
    else if (item.output_bytes && item.total_bytes)
    {
      summary = "truncated " + std::to_string(*item.output_bytes) + "/" + std::to_string(*item.total_bytes) + " bytes";
    }
    else
    {
      summary = "truncated output";
    }
    if (item.next_offset_line)
      summary += "; next offset " + std::to_string(*item.next_offset_line);
    std::vector<std::string> omitted;
    if (item.omitted_bytes)
      omitted.push_back(std::to_string(*item.omitted_bytes) + " bytes");
    if (item.omitted_lines)
      omitted.push_back(std::to_string(*item.omitted_lines) + " lines");
    if (!omitted.empty())
    {
      summary += "; omitted ";
      for (std::size_t index = 0; index < omitted.size(); ++index)
      {
        if (index > 0)
          summary += ", ";
        summary += omitted[index];
      }
    }
  }
  if (!item.spill_path.empty())
  {
    if (!summary.empty())
      summary += "; ";
    summary += "spill " + item.spill_path;
  }
  if (item.spill_truncated)
  {
    if (!summary.empty())
      summary += "; ";
    summary += "spill truncated";
  }
  return summary;
}

std::string status_marker(ToolTimelineStatus status)
{
  switch (status)
  {
    case ToolTimelineStatus::Running:
      return "[~]";
    case ToolTimelineStatus::Success:
      return "[+]";
    case ToolTimelineStatus::Error:
      return "[x]";
  }
  return "[?]";
}

std::string_view status_sgr(ToolTimelineStatus status)
{
  switch (status)
  {
    case ToolTimelineStatus::Running:
      return detail::kSgrWarning;
    case ToolTimelineStatus::Success:
      return detail::kSgrSuccess;
    case ToolTimelineStatus::Error:
      return detail::kSgrError;
  }
  return detail::kSgrDim;
}

}  // namespace

std::string to_string(ToolLifecycleState state)
{
  switch (state)
  {
    case ToolLifecycleState::ProviderAnnounced:
      return "announced";
    case ToolLifecycleState::ArgumentsStreaming:
      return "arguments streaming";
    case ToolLifecycleState::ArgumentsComplete:
      return "arguments complete";
    case ToolLifecycleState::ExecutionStarted:
      return "executing";
    case ToolLifecycleState::Progress:
      return "progress";
    case ToolLifecycleState::Complete:
      return "complete";
    case ToolLifecycleState::Error:
      return "error";
  }
  return "unknown";
}

namespace detail {

bool tool_card_details_visible(ToolTimelineItem const& item, bool global_details_visible)
{
  return item.details_visible.value_or(global_details_visible);
}

std::vector<std::string> render_tool_card(ToolTimelineItem const& item, std::size_t width, bool global_details_visible)
{
  std::vector<std::string> lines;
  auto const details_visible = tool_card_details_visible(item, global_details_visible);

  auto marker = status_marker(item.status);
  auto name_raw = sanitize_terminal_text(item.name.empty() ? "unknown" : item.name);
  auto args_raw = sanitize_terminal_text(item.argument_summary);
  auto lifecycle_raw = sanitize_terminal_text(to_string(item.lifecycle));
  auto const is_shell = shell_tool(item);
  auto const command = command_text(item);
  auto const output = output_preview_text(item);
  auto const shell_status = is_shell ? exit_status_text(item) : std::optional<std::string>{};
  auto const shell_duration = is_shell ? duration_text(item) : std::optional<std::string>{};
  if (args_raw.empty() && command)
    args_raw = sanitize_terminal_text(*command);

  auto prefix_text = wide_blocks(width) ? std::string("  │ ") : std::string("  ");
  auto prefix_cols = terminal_text_columns(prefix_text + marker + " ");
  auto name_cols = terminal_text_columns(name_raw);
  auto lifecycle_cols = terminal_text_columns(std::string("  ") + lifecycle_raw);
  auto args_cols = args_raw.empty() ? 0 : terminal_text_columns(std::string("  ") + args_raw);

  if (prefix_cols + name_cols + lifecycle_cols + args_cols > width)
  {
    if (!args_raw.empty() && width > prefix_cols + name_cols + lifecycle_cols)
    {
      auto budget = width - prefix_cols - name_cols - lifecycle_cols;
      args_raw = fit_line(std::move(args_raw), budget);
      args_cols = terminal_text_columns(args_raw);
    }
    if (prefix_cols + name_cols + lifecycle_cols + args_cols > width && width > prefix_cols + lifecycle_cols)
    {
      auto budget = width - prefix_cols - lifecycle_cols;
      name_raw = fit_line(std::move(name_raw), budget);
    }
  }

  std::string line1 = prefix_text + std::string(status_sgr(item.status)) + marker + std::string(kSgrReset) + " " + std::string(kSgrBold) +
                      std::string(kSgrAccent) + name_raw + std::string(kSgrReset) + "  " + std::string(kSgrMuted) + lifecycle_raw + std::string(kSgrReset);
  if (!args_raw.empty())
  {
    line1 += "  " + std::string(kSgrDim) + args_raw + std::string(kSgrReset);
  }
  lines.push_back(fit_line_preserving_sgr(line1, width));

  auto const truncation = truncation_summary(item);
  if (details_visible)
  {
    append_tool_detail_lines(lines, "args", item.argument_summary, width);
    if (is_shell && command)
      append_tool_detail_lines(lines, "command", *command, width);
    if (is_shell && shell_status)
      append_tool_detail_lines(lines, "status", *shell_status, width);
    if (is_shell && shell_duration)
      append_tool_detail_lines(lines, "duration", *shell_duration, width);
    if (is_shell && item.status == ToolTimelineStatus::Running)
      append_tool_detail_lines(lines, "cancel", "Esc or Ctrl+C requests stop", width);
    append_tool_detail_lines(lines, "result", item.result_summary, width);
    if (output)
      append_output_preview_lines(lines, item, "output", *output, width, kExpandedOutputPreviewLines);
    if (!truncation.empty())
      append_tool_detail_lines(lines, "truncation", truncation, width);
    if (!item.spill_path.empty())
      append_tool_detail_lines(lines, "spill", item.spill_path, width);
    append_diff_lines(lines, item, width);
  }
  else
  {
    auto compact = !item.result_summary.empty() ? item.result_summary : truncation;
    if (is_shell && shell_status)
    {
      compact = *shell_status;
      if (shell_duration)
        compact += " · " + *shell_duration;
      if (item.status == ToolTimelineStatus::Running)
        compact += " · Esc/Ctrl+C stop";
    }
    if (!compact.empty())
    {
      auto result_raw = sanitize_terminal_text(compact);
      std::string line2 = (wide_blocks(width) ? std::string("  │     ") : std::string("      ")) + std::string(kSgrMuted) + result_raw + std::string(kSgrReset);
      lines.push_back(fit_line_preserving_sgr(line2, width));
    }
    if (output)
      append_output_preview_lines(lines, item, "output", *output, width, kCollapsedOutputPreviewLines);
    if (!item.result_summary.empty() && !truncation.empty())
    {
      std::string line3 = (wide_blocks(width) ? std::string("  │     ") : std::string("      ")) + std::string(kSgrWarning) +
                          sanitize_terminal_text(truncation) + std::string(kSgrReset);
      lines.push_back(fit_line_preserving_sgr(line3, width));
    }
  }

  return lines;
}

}  // namespace detail
}  // namespace ava::tui
