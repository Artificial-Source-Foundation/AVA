#include "sys.h"
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
constexpr auto kExpandedOutputPreviewLines = std::size_t{8};
constexpr auto kExpandedDiffPreviewLines = std::size_t{14};
constexpr auto kExpandedInputPreviewColumns = std::size_t{512};
constexpr auto kExactOutputPreviewLineCountMaxBytes = std::size_t{64 * 1024};
constexpr auto kWidePermissionDetailWidth = std::size_t{72};

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

std::optional<std::size_t> csi_sequence_end(std::string_view text, std::size_t index)
{
  for (auto scan = index; scan < text.size(); ++scan)
  {
    auto const byte = static_cast<unsigned char>(text[scan]);
    if (byte >= 0x40 && byte <= 0x7E)
      return scan + 1;
  }
  return std::nullopt;
}

std::optional<std::size_t> terminal_control_sequence_end(std::string_view text, std::size_t index)
{
  auto const byte = static_cast<unsigned char>(text[index]);
  if (byte != 0x1B || index + 1 >= text.size())
    return std::nullopt;

  auto const introducer = text[index + 1];
  if (introducer == '[')
    return csi_sequence_end(text, index + 2);
  if (introducer != ']')
    return std::nullopt;

  for (auto scan = index + 2; scan < text.size(); ++scan)
  {
    auto const current = static_cast<unsigned char>(text[scan]);
    if (current == '\a')
      return scan + 1;
    if (current == 0x1B && scan + 1 < text.size() && text[scan + 1] == '\\')
      return scan + 2;
  }
  return std::nullopt;
}

std::string sanitize_copy_text(std::string_view text)
{
  std::string stripped;
  stripped.reserve(text.size());
  for (std::size_t index = 0; index < text.size();)
  {
    if (auto end = terminal_control_sequence_end(text, index))
    {
      index = *end;
      continue;
    }
    stripped.push_back(text[index]);
    ++index;
  }
  return sanitize_terminal_text(stripped);
}

bool ascii_whitespace(char ch)
{
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

std::string sanitized_payload_identity(std::string_view text)
{
  std::string stripped;
  stripped.reserve(text.size());
  for (std::size_t index = 0; index < text.size();)
  {
    if (auto end = terminal_control_sequence_end(text, index))
    {
      index = *end;
      continue;
    }
    stripped.push_back(text[index]);
    ++index;
  }
  while (!stripped.empty() && ascii_whitespace(stripped.back())) stripped.pop_back();

  std::string sanitized;
  sanitized.reserve(stripped.size());
  std::size_t start = 0;
  for (std::size_t index = 0; index < stripped.size(); ++index)
  {
    if (!ascii_whitespace(stripped[index]) || stripped[index] == ' ')
      continue;
    sanitized += sanitize_terminal_text(std::string_view(stripped).substr(start, index - start));
    sanitized.push_back(stripped[index]);
    start = index + 1;
  }
  sanitized += sanitize_terminal_text(std::string_view(stripped).substr(start));
  return sanitized;
}

bool same_payload(std::string_view lhs, std::string_view rhs)
{
  auto const left = sanitized_payload_identity(lhs);
  return !left.empty() && left == sanitized_payload_identity(rhs);
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

bool permission_audit_payload(ToolTimelineItem const& item, std::string_view text)
{
  if (item.permissions.empty() || !shell_tool(item))
    return false;

  bool has_permission_request_id = false;
  for (auto const& id : item.permission_request_ids)
  {
    has_permission_request_id = has_permission_request_id || (!id.empty() && text.find(id) != std::string_view::npos);
  }
  for (auto const& audit : item.permissions)
  {
    has_permission_request_id =
        has_permission_request_id || (!audit.permission_request_id.empty() && text.find(audit.permission_request_id) != std::string_view::npos);
  }
  if (!has_permission_request_id)
    return false;

  auto const lowered = lower_ascii(text);
  if (lowered.find("permission_denied") != std::string::npos)
    return true;

  return lowered.find("action:") != std::string::npos || lowered.find("request_id:") != std::string::npos || lowered.find("inspect:") != std::string::npos ||
         lowered.find("decision:") != std::string::npos || lowered.find("resolution:") != std::string::npos ||
         lowered.find("operation:") != std::string::npos || lowered.find("risk:") != std::string::npos || lowered.find("reason:") != std::string::npos ||
         lowered.find("target:") != std::string::npos || lowered.find("command:") != std::string::npos ||
         lowered.find("/permissions audit") != std::string::npos || lowered.find("/permissions diagnose") != std::string::npos;
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

struct OutputPreviewLines
{
  std::vector<std::string_view> visible = {};
  bool has_more = false;
  std::optional<std::size_t> exact_total_lines = std::nullopt;
};

OutputPreviewLines output_preview_lines(std::string_view text, std::size_t max_visible)
{
  OutputPreviewLines preview;
  if (text.empty() || max_visible == 0)
    return preview;
  auto const count_exact = text.size() <= kExactOutputPreviewLineCountMaxBytes;
  preview.visible.reserve(max_visible);
  std::size_t total_lines = 0;
  std::size_t start = 0;
  auto append_line = [&](std::size_t end) {
    ++total_lines;
    if (preview.visible.size() < max_visible)
      preview.visible.push_back(text.substr(start, end - start));
    else
      preview.has_more = true;
  };
  for (std::size_t index = 0; index < text.size(); ++index)
  {
    if (text[index] != '\n' && text[index] != '\r')
      continue;

    append_line(index);
    if (preview.has_more && !count_exact)
      return preview;
    if (text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n')
      ++index;
    start = index + 1;
  }
  if (start < text.size())
    append_line(text.size());
  if (count_exact)
    preview.exact_total_lines = total_lines;
  return preview;
}

std::size_t known_total_output_lines(ToolTimelineItem const& item, OutputPreviewLines const& preview)
{
  auto const visible_lines = preview.visible.size();
  auto local_minimum = preview.exact_total_lines.value_or(visible_lines + (preview.has_more ? std::size_t{1} : std::size_t{0}));
  auto total = item.total_lines.value_or(local_minimum);
  total = std::max(total, local_minimum);
  if (item.omitted_lines)
    total = std::max(total, local_minimum + *item.omitted_lines);
  return total;
}

void append_output_preview_lines(std::vector<std::string>& lines, ToolTimelineItem const& item, std::string_view label, std::string const& text,
                                 std::size_t width, std::size_t max_visible)
{
  if (text.empty() || max_visible == 0)
    return;
  auto preview = output_preview_lines(text, max_visible);
  if (preview.visible.size() == 1 && preview.visible.front().empty())
    return;

  auto const prefix = wide_blocks(width) ? std::string("  │     ") : std::string("      ");
  auto const content_prefix = wide_blocks(width) ? std::string("  │       ") : std::string("        ");
  auto const visible = preview.visible.size();
  auto const known_total = known_total_output_lines(item, preview);
  auto const hidden_by_total = known_total > visible ? known_total - visible : std::size_t{0};
  auto hidden_by_local = std::size_t{0};
  if (preview.exact_total_lines && *preview.exact_total_lines > visible)
  {
    hidden_by_local = *preview.exact_total_lines - visible;
  }
  else if (preview.has_more)
  {
    hidden_by_local = 1;
  }
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
    auto sanitized = sanitize_terminal_text(preview.visible[index]);
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

std::vector<std::string> display_changed_paths(ToolTimelineItem const& item)
{
  auto paths = item.changed_paths;
  auto const append_path = [&paths](std::string path) {
    if (path.empty())
      return;
    if (std::ranges::find(paths, path) == paths.end())
      paths.push_back(std::move(path));
  };
  for (auto const& path : ava::core::json::strings_in_array_field(item.result_json, "changed_paths")) append_path(path);
  for (auto const& path : ava::core::json::strings_in_array_field(item.result_json, "changed_files")) append_path(path);
  for (auto const& edit : ava::core::json::objects_in_array_field(item.result_json, "edits"))
    append_path(ava::core::json::string_field(edit, "path").value_or(""));
  if (item.name == "write" || item.name == "write_file" || item.name == "edit_file" || item.name == "apply_patch")
  {
    if (auto path = first_json_string(item.result_json, {"path", "target_path"}))
      append_path(*path);
    if (auto path = first_json_string(item.arguments_json, {"path", "target_path"}))
      append_path(*path);
  }

  return paths;
}

bool proven_relative_path_alias(std::string_view absolute, std::string_view relative)
{
  return absolute.starts_with('/') && !relative.empty() && !relative.starts_with('/') && absolute.size() > relative.size() && absolute.ends_with(relative) &&
         absolute[absolute.size() - relative.size() - 1] == '/';
}

std::optional<std::string> mutation_argument_path(ToolTimelineItem const& item)
{
  auto const mutation = item.name == "write" || item.name == "write_file" || item.name == "edit_file" || item.name == "apply_patch";
  if (!mutation || item.argument_summary.empty())
    return std::nullopt;

  auto path = std::string_view(item.argument_summary);
  if (path.starts_with("path="))
  {
    path.remove_prefix(std::string_view("path=").size());
    if (auto const comma = path.find(','); comma != std::string_view::npos)
      path = path.substr(0, comma);
    while (!path.empty() && path.front() == ' ') path.remove_prefix(1);
    while (!path.empty() && path.back() == ' ') path.remove_suffix(1);
  }
  else if (item.name != "write")
  {
    return std::nullopt;
  }

  if (path.empty() || path.starts_with('/') || path.find_first_of("\r\n") != std::string_view::npos)
    return std::nullopt;
  return std::string(path);
}

std::string project_display_path_aliases(ToolTimelineItem const& item, std::string_view text)
{
  auto const paths = display_changed_paths(item);
  auto const argument_path = mutation_argument_path(item);
  if (!argument_path || paths.size() != 1 || !proven_relative_path_alias(paths.front(), *argument_path))
    return std::string(text);

  std::string projected(text);
  for (auto offset = projected.find(paths.front()); offset != std::string::npos;
       offset = projected.find(paths.front(), offset + argument_path->size()))
  {
    projected.replace(offset, paths.front().size(), *argument_path);
  }
  return projected;
}

std::vector<std::string> projected_display_changed_paths(ToolTimelineItem const& item)
{
  std::vector<std::string> projected;
  for (auto const& path : display_changed_paths(item))
  {
    auto alias = project_display_path_aliases(item, path);
    if (std::ranges::find(projected, alias) == projected.end())
      projected.push_back(std::move(alias));
  }
  return projected;
}

std::string changed_paths_summary(ToolTimelineItem const& item, bool project_aliases = false)
{
  auto const paths = project_aliases ? projected_display_changed_paths(item) : display_changed_paths(item);
  if (paths.empty())
    return {};

  std::string summary;
  for (std::size_t index = 0; index < paths.size() && index < 4; ++index)
  {
    if (index > 0)
      summary += ", ";
    summary += sanitize_terminal_text(paths[index]);
  }
  if (paths.size() > 4)
    summary += ", +" + std::to_string(paths.size() - 4) + " more";
  return summary;
}

void append_diff_lines(std::vector<std::string>& lines, ToolTimelineItem const& item, std::size_t width)
{
  if (item.diff.empty())
    return;
  auto const prefix = wide_blocks(width) ? std::string("  │     ") : std::string("      ");
  auto label = std::string("diff");
  auto const paths = projected_display_changed_paths(item);
  if (!paths.empty())
  {
    label += " ";
    for (std::size_t index = 0; index < paths.size() && index < 3; ++index)
    {
      if (index > 0)
        label += ", ";
      label += sanitize_terminal_text(paths[index]);
    }
    if (paths.size() > 3)
      label += ", +" + std::to_string(paths.size() - 3) + " more";
  }
  lines.push_back(detail::fit_line_preserving_sgr(prefix + std::string(detail::kSgrDim) + std::move(label) + ":" + std::string(detail::kSgrReset), width));
  auto const content_prefix = wide_blocks(width) ? std::string("  │       ") : std::string("        ");
  auto const diff = project_display_path_aliases(item, item.diff);
  auto diff_lines = detail::render_unified_diff_body(diff, item.diff_truncated, width, content_prefix, kExpandedDiffPreviewLines);
  lines.insert(lines.end(), diff_lines.begin(), diff_lines.end());
}

std::string truncation_summary(ToolTimelineItem const& item)
{
  if (!item.truncated)
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
  return summary;
}

std::string byte_retention_summary(ToolTimelineItem const& item)
{
  if (!item.output_bytes || !item.total_bytes)
    return {};
  return std::to_string(*item.output_bytes) + "/" + std::to_string(*item.total_bytes) + " bytes retained";
}

std::string permission_decision_label(ToolPermissionAuditItem const& audit)
{
  auto const decision = lower_ascii(audit.decision);
  if (decision == "allow" || decision == "allowed")
    return "allow";
  if (decision == "allow_session" || decision == "allow_session_grant" || decision == "session_grant")
    return "allow session";
  if (decision == "allow_remember")
    return "allow always";
  if (decision == "deny" || decision == "denied")
    return "deny";
  return "checked";
}

std::string permission_state_label(ToolTimelineItem const& item, ToolPermissionAuditItem const& audit)
{
  if (audit.decision.empty() && item.status == ToolTimelineStatus::Running)
    return "required";
  return permission_decision_label(audit);
}

bool permission_decision_denied(ToolPermissionAuditItem const& audit)
{
  return permission_decision_label(audit) == "deny";
}

std::string permission_audit_detail(ToolTimelineItem const& item, ToolPermissionAuditItem const& audit)
{
  std::vector<std::string> parts;
  parts.push_back(permission_state_label(item, audit));
  if (!audit.risk.empty())
    parts.push_back("risk " + audit.risk);
  if (!audit.permission_request_id.empty())
    parts.push_back("id " + audit.permission_request_id);
  if (!audit.resolver_request_id.empty())
    parts.push_back("resolver " + audit.resolver_request_id);
  if (!audit.reason.empty())
    parts.push_back("reason " + audit.reason);
  if (!audit.resolution_reason.empty())
    parts.push_back("resolution " + audit.resolution_reason);
  if (!audit.operation.empty())
    parts.push_back("operation " + audit.operation);
  if (!audit.tool_name.empty())
    parts.push_back("tool " + audit.tool_name);
  if (!audit.target.empty())
    parts.push_back("target " + audit.target);
  if (!audit.command.empty())
    parts.push_back("command " + audit.command);

  std::string detail;
  for (std::size_t index = 0; index < parts.size(); ++index)
  {
    if (index > 0)
      detail += " · ";
    detail += parts[index];
  }
  return detail;
}

std::string permission_audit_show_command(ToolPermissionAuditItem const& audit)
{
  if (audit.permission_request_id.empty())
    return {};
  return "/permissions audit show " + audit.permission_request_id;
}

std::string permission_diagnose_command(ToolPermissionAuditItem const& audit)
{
  if (audit.permission_request_id.empty() || !permission_decision_denied(audit))
    return {};
  return "/permissions diagnose " + audit.permission_request_id;
}

std::string permission_ids_summary(ToolTimelineItem const& item)
{
  if (item.permission_request_ids.empty())
    return {};
  auto const pending = item.status == ToolTimelineStatus::Running;
  if (item.permission_request_ids.size() == 1)
    return pending ? "permission required" : "permission checked";
  return pending ? "permissions required (" + std::to_string(item.permission_request_ids.size()) + ")"
                 : "permissions checked (" + std::to_string(item.permission_request_ids.size()) + ")";
}

std::string permission_summary(ToolTimelineItem const& item)
{
  if (item.permissions.empty())
    return permission_ids_summary(item);
  if (item.permissions.size() == 1)
  {
    auto const& audit = item.permissions.front();
    auto summary = "permission " + permission_state_label(item, audit);
    if (!audit.risk.empty())
      summary += " · risk " + audit.risk;
    if (!audit.reason.empty())
      summary += " · reason " + audit.reason;
    return summary;
  }

  bool saw_required = false;
  bool saw_deny = false;
  bool saw_allow = false;
  std::string risk;
  for (auto const& audit : item.permissions)
  {
    auto const decision = permission_state_label(item, audit);
    saw_required = saw_required || decision == "required";
    saw_deny = saw_deny || decision == "deny";
    saw_allow = saw_allow || decision.starts_with("allow");
    if (risk.empty() && !audit.risk.empty())
      risk = audit.risk;
  }

  auto summary = "permissions " + std::to_string(item.permissions.size());
  if (saw_required)
    summary += " required";
  else if (saw_deny)
    summary += " · deny";
  else if (saw_allow)
    summary += " · allow";
  else
    summary += " checked";
  if (!risk.empty())
    summary += " · risk " + risk;
  return summary;
}

void append_permission_lines(std::vector<std::string>& lines, ToolTimelineItem const& item, std::size_t width)
{
  if (item.permissions.empty())
  {
    auto ids = permission_ids_summary(item);
    if (ids.empty())
      return;
    if (width >= kWidePermissionDetailWidth)
    {
      append_tool_detail_lines(lines, "permission", ids, width);
      return;
    }
    auto const state = item.status == ToolTimelineStatus::Running ? "required" : "checked";
    append_tool_detail_lines(lines, "permission",
                             item.permission_request_ids.size() == 1 ? state : std::to_string(item.permission_request_ids.size()) + " " + state, width);
    for (auto const& id : item.permission_request_ids) append_tool_detail_lines(lines, "id", id, width);
    return;
  }
  for (auto const& audit : item.permissions)
  {
    if (width >= kWidePermissionDetailWidth)
    {
      append_tool_detail_lines(lines, "permission", permission_audit_detail(item, audit), width);
      continue;
    }
    append_tool_detail_lines(lines, "permission", permission_state_label(item, audit), width);
    append_tool_detail_lines(lines, "risk", audit.risk, width);
    append_tool_detail_lines(lines, "id", audit.permission_request_id, width);
    append_tool_detail_lines(lines, "resolver", audit.resolver_request_id, width);
    append_tool_detail_lines(lines, "reason", audit.reason, width);
    append_tool_detail_lines(lines, "resolution", audit.resolution_reason, width);
    append_tool_detail_lines(lines, "operation", audit.operation, width);
    append_tool_detail_lines(lines, "tool", audit.tool_name, width);
    append_tool_detail_lines(lines, "target", audit.target, width);
    append_tool_detail_lines(lines, "command", audit.command, width);
  }
  for (auto const& audit : item.permissions)
  {
    if (auto inspect = permission_audit_show_command(audit); !inspect.empty())
      append_tool_detail_lines(lines, "inspect", inspect, width);
    if (auto diagnose = permission_diagnose_command(audit); !diagnose.empty())
      append_tool_detail_lines(lines, "diagnose", diagnose, width);
  }
}

std::string status_marker(ToolTimelineStatus status)
{
  switch (status)
  {
    case ToolTimelineStatus::Running:
      return "~";
    case ToolTimelineStatus::Success:
      return "+";
    case ToolTimelineStatus::Canceled:
      return "-";
    case ToolTimelineStatus::Error:
      return "x";
  }
  return "?";
}

std::string_view status_sgr(ToolTimelineStatus status)
{
  switch (status)
  {
    case ToolTimelineStatus::Running:
      return detail::kSgrWarning;
    case ToolTimelineStatus::Success:
      return detail::kSgrSuccess;
    case ToolTimelineStatus::Canceled:
      return detail::kSgrDim;
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
    case ToolLifecycleState::Canceled:
      return "canceled";
    case ToolLifecycleState::Error:
      return "error";
  }
  return "unknown";
}

namespace detail {

void append_copy_block(std::string& output, std::string_view label, std::string const& text)
{
  if (text.empty())
    return;
  auto const lines = split_lines(text);
  if (lines.empty())
    return;
  if (lines.size() == 1)
  {
    output += std::string(label) + ": " + sanitize_copy_text(lines.front()) + "\n";
    return;
  }
  output += std::string(label) + ":\n";
  for (auto const& line : lines) output += "  " + sanitize_copy_text(line) + "\n";
}

bool tool_card_details_visible(ToolTimelineItem const& item, bool global_details_visible)
{
  return item.details_visible.value_or(global_details_visible);
}

bool permission_matches_copy_query(ToolPermissionAuditItem const& permission, std::string_view query)
{
  if (query.empty())
    return true;
  auto const needle = lower_ascii(query);
  auto const matches = [&needle](std::string_view value) { return !value.empty() && lower_ascii(value).find(needle) != std::string::npos; };
  return matches(permission.permission_request_id) || matches(permission.resolver_request_id) || matches(permission.decision) ||
         matches(permission.operation) || matches(permission.tool_name) || matches(permission.risk) || matches(permission.reason) ||
         matches(permission.target) || matches(permission.command) || matches(permission.resolution_reason) ||
         matches(permission_audit_show_command(permission)) || matches(permission_diagnose_command(permission));
}

bool tool_card_matches_copy_query(ToolTimelineItem const& item, std::string_view query)
{
  if (query.empty())
    return true;
  auto const needle = lower_ascii(query);
  auto const matches = [&needle](std::string_view value) { return !value.empty() && lower_ascii(value).find(needle) != std::string::npos; };

  if (matches(item.name) || matches(item.argument_summary) || matches(item.result_summary) || matches(item.arguments_json) || matches(item.result_json) ||
      matches(item.call_id) || matches(item.request_id) || matches(item.correlation_id) || matches(item.diff) || matches(item.spill_path))
    return true;
  for (auto const& path : display_changed_paths(item))
    if (matches(path))
      return true;
  for (auto const& id : item.permission_request_ids)
    if (matches(id))
      return true;
  for (auto const& permission : item.permissions)
  {
    if (permission_matches_copy_query(permission, query))
      return true;
  }
  return false;
}

std::string tool_card_diff_copy_text(ToolTimelineItem const& item)
{
  if (item.diff.empty())
    return {};
  std::string output;
  for (auto const& line : split_lines(item.diff))
  {
    if (!output.empty())
      output += '\n';
    output += sanitize_copy_text(line);
  }
  if (item.diff_truncated)
  {
    if (!output.empty())
      output += '\n';
    output += "[diff truncated]";
  }
  return output;
}

std::string tool_card_permission_copy_text(ToolTimelineItem const& item, std::string_view query)
{
  std::string output;
  for (auto const& audit : item.permissions)
  {
    if (!permission_matches_copy_query(audit, query))
      continue;
    append_copy_block(output, "permission", permission_audit_detail(item, audit));
    append_copy_block(output, "inspect", permission_audit_show_command(audit));
    append_copy_block(output, "diagnose", permission_diagnose_command(audit));
  }
  if (!output.empty() && output.back() == '\n')
    output.pop_back();
  return output;
}

std::string tool_card_copy_text(ToolTimelineItem const& item)
{
  std::string output;
  append_copy_block(output, "tool", item.name.empty() ? std::string("unknown") : item.name);
  append_copy_block(output, "status", ava::tui::to_string(item.status));
  append_copy_block(output, "lifecycle", ava::tui::to_string(item.lifecycle));
  if (!permission_audit_payload(item, item.argument_summary))
    append_copy_block(output, "args", item.argument_summary);
  append_copy_block(output, "input", item.arguments_json);
  append_copy_block(output, "call id", item.call_id);
  append_copy_block(output, "request id", item.request_id);
  append_copy_block(output, "correlation id", item.correlation_id);

  auto const is_shell = shell_tool(item);
  auto const shell_status = is_shell ? exit_status_text(item) : std::optional<std::string>{};
  if (is_shell)
  {
    if (auto command = command_text(item); command && !permission_audit_payload(item, *command))
      append_copy_block(output, "command", *command);
    if (shell_status && !permission_audit_payload(item, *shell_status))
      append_copy_block(output, "shell status", *shell_status);
    if (auto shell_duration = duration_text(item))
      append_copy_block(output, "duration", *shell_duration);
  }

  for (auto const& audit : item.permissions)
  {
    append_copy_block(output, "permission", permission_audit_detail(item, audit));
    append_copy_block(output, "inspect", permission_audit_show_command(audit));
    append_copy_block(output, "diagnose", permission_diagnose_command(audit));
  }
  if (!permission_audit_payload(item, item.result_summary) && (item.truncated || !shell_status || !same_payload(item.result_summary, *shell_status)))
    append_copy_block(output, "result", item.result_summary);
  if (auto output_preview = output_preview_text(item);
      output_preview && !permission_audit_payload(item, *output_preview) &&
      (item.truncated || (!same_payload(*output_preview, item.result_summary) && (!shell_status || !same_payload(*output_preview, *shell_status)))))
    append_copy_block(output, "output", *output_preview);
  auto const truncation = truncation_summary(item);
  append_copy_block(output, "truncation", truncation);
  if (shell_tool(item))
    append_copy_block(output, "bytes", byte_retention_summary(item));
  append_copy_block(output, "changed", changed_paths_summary(item));
  append_copy_block(output, "full output", item.spill_path);
  if (item.spill_truncated)
    append_copy_block(output, "spill incomplete", "true");
  append_copy_block(output, "diff", item.diff);
  if (!output.empty() && output.back() == '\n')
    output.pop_back();
  return output;
}

namespace {

std::optional<std::size_t> absolute_path_start(std::string_view text)
{
  for (std::size_t index = 0; index < text.size(); ++index)
  {
    if (text[index] != '/')
      continue;
    if (index == 0 || text[index - 1] == ' ' || text[index - 1] == '=' || text[index - 1] == '\'' || text[index - 1] == '"')
      return index;
  }
  return std::nullopt;
}

bool contains_absolute_path(std::string_view text)
{
  return absolute_path_start(text).has_value();
}

std::string resting_changed_paths_summary(ToolTimelineItem const& item)
{
  for (auto const& path : display_changed_paths(item))
  {
    auto const sanitized = sanitize_terminal_text(path);
    if (!sanitized.empty() && sanitized == path && !contains_absolute_path(sanitized) && sanitized.find_first_of("\r\n") == std::string::npos)
      return sanitized;
  }
  return {};
}

std::string resting_text(ToolTimelineItem const& item, std::string_view text)
{
  if (permission_audit_payload(item, text))
    return {};
  auto sanitized = sanitize_terminal_text(text);
  if (contains_absolute_path(sanitized))
    return {};
  for (auto const id : {std::string_view(item.call_id), std::string_view(item.request_id), std::string_view(item.correlation_id)})
  {
    if (!id.empty() && sanitized.find(id) != std::string::npos)
      return {};
  }
  for (auto const& id : item.permission_request_ids)
  {
    if (!id.empty() && sanitized.find(id) != std::string::npos)
      return {};
  }
  for (auto const& audit : item.permissions)
  {
    if ((!audit.permission_request_id.empty() && sanitized.find(audit.permission_request_id) != std::string::npos) ||
        (!audit.resolver_request_id.empty() && sanitized.find(audit.resolver_request_id) != std::string::npos))
      return {};
  }
  if (sanitized.find("/permissions audit") != std::string::npos || sanitized.find("/permissions diagnose") != std::string::npos)
    return {};
  return sanitized;
}

std::string resting_result_text(ToolTimelineItem const& item)
{
  if (permission_audit_payload(item, item.result_summary))
    return {};
  auto result = sanitize_terminal_text(item.result_summary);
  if (auto const path_start = absolute_path_start(result))
  {
    result.resize(*path_start);
    while (!result.empty() && result.back() == ' ') result.pop_back();
    if (result.ends_with(" to"))
    {
      result.resize(result.size() - 3);
      while (!result.empty() && result.back() == ' ') result.pop_back();
    }
  }
  return resting_text(item, result);
}

std::string tool_primary_summary(ToolTimelineItem const& item, bool suppress_result_summary, bool is_shell, std::optional<std::string> const& command,
                                 std::optional<std::string> const& shell_status, std::optional<std::string> const& duration, std::string const& truncation)
{
  auto const with_duration = [&](std::string text) {
    if (item.status != ToolTimelineStatus::Running && duration && !text.empty() && text.find(*duration) == std::string::npos)
      text += " · " + *duration;
    return text;
  };
  auto const argument_or_command = [&]() {
    if (auto summary = resting_text(item, item.argument_summary); !summary.empty())
      return summary;
    if (command)
      return resting_text(item, *command);
    return std::string{};
  };

  auto target = item.status == ToolTimelineStatus::Running ? argument_or_command() : resting_changed_paths_summary(item);
  if (target.empty())
    target = argument_or_command();
  if (item.status == ToolTimelineStatus::Running)
  {
    auto const pending = item.lifecycle == ToolLifecycleState::ProviderAnnounced || item.lifecycle == ToolLifecycleState::ArgumentsStreaming ||
                         item.lifecycle == ToolLifecycleState::ArgumentsComplete;
    auto state = pending ? std::string("pending") : std::string("running");
    return target.empty() ? state : state + " · " + target;
  }
  if (is_shell && shell_status && (!suppress_result_summary || sanitize_terminal_text(*shell_status) != sanitize_terminal_text(item.result_summary)))
  {
    auto text = resting_text(item, *shell_status);
    if (!text.empty())
    {
      if (item.status == ToolTimelineStatus::Canceled && text.find("canceled") == std::string::npos)
        text = "canceled · " + text;
      return with_duration(std::move(text));
    }
  }
  auto const result = suppress_result_summary ? std::string{} : resting_result_text(item);
  auto const sanitized_target = sanitize_terminal_text(target);
  auto const sanitized_result = sanitize_terminal_text(result);
  auto const target_value_offset = sanitized_target.find('=');
  auto const target_value = target_value_offset == std::string::npos ? std::string_view{} : std::string_view(sanitized_target).substr(target_value_offset + 1);
  auto const result_contains_target =
      sanitized_result.find(sanitized_target) != std::string::npos || (!target_value.empty() && sanitized_result.find(target_value) != std::string::npos);
  if (!target.empty() && !result.empty() && sanitized_target != sanitized_result && !result_contains_target)
    return with_duration(target + " · " + result);
  if (!result.empty())
    return with_duration(result);
  if (!target.empty())
    return with_duration(target);
  if (!truncation.empty())
    return with_duration(truncation);
  if (item.status == ToolTimelineStatus::Canceled)
    return "canceled";
  if (item.status == ToolTimelineStatus::Error)
    return "failed";
  return {};
}

bool detail_repeats_primary(std::string const& detail, std::string const& primary)
{
  if (detail.empty() || primary.empty())
    return false;
  if (same_payload(detail, primary))
    return true;
  std::size_t offset = 0;
  while (offset <= primary.size())
  {
    auto const separator = primary.find(" · ", offset);
    auto const part = primary.substr(offset, separator == std::string::npos ? std::string::npos : separator - offset);
    if (same_payload(detail, part))
      return true;
    if (separator == std::string::npos)
      break;
    offset = separator + std::string_view(" · ").size();
  }
  return false;
}

std::optional<std::string> input_preview_text(ToolTimelineItem const& item, std::optional<std::string> const& command)
{
  constexpr auto kInputPreviewSourceBytes = kExpandedInputPreviewColumns * std::size_t{4};
  if (item.arguments_json.empty())
    return std::nullopt;
  auto const compare_complete_input = item.arguments_json.size() <= kInputPreviewSourceBytes;
  if (compare_complete_input && (sanitized_payload_identity(item.arguments_json).empty() || same_payload(item.arguments_json, item.argument_summary) ||
                                 (command && same_payload(item.arguments_json, *command))))
    return std::nullopt;

  auto source = std::string(std::string_view(item.arguments_json).substr(0, kInputPreviewSourceBytes));
  if (source.size() < item.arguments_json.size())
    source += "...";
  auto preview = fit_line(std::move(source), kExpandedInputPreviewColumns);
  auto sanitized = sanitize_copy_text(preview);
  if (sanitized.empty() || same_payload(sanitized, item.argument_summary) || (command && same_payload(sanitized, *command)))
    return std::nullopt;
  return sanitized;
}

bool tool_action_line_fits(std::string_view label, std::string_view command, std::size_t width)
{
  auto const prefix = wide_blocks(width) ? std::string_view("  │     ") : std::string_view("      ");
  return terminal_text_columns(prefix) + terminal_text_columns(label) + terminal_text_columns(": ") + terminal_text_columns(command) <= width;
}

bool tool_action_rows_fit(ToolTimelineItem const& item, std::string_view query, std::size_t width)
{
  if (!tool_action_line_fits("toggle", "/tool " + std::string(query), width) || !tool_action_line_fits("copy", "/copy tool " + std::string(query), width))
    return false;
  if (item.diff.empty())
    return true;
  return tool_action_line_fits("diff", "/diff " + std::string(query), width) && tool_action_line_fits("copy diff", "/copy diff " + std::string(query), width);
}

std::string stable_tool_query(ToolTimelineItem const& item, std::size_t width)
{
  auto const safe_query = [](std::string_view value, bool require_relative) {
    auto const sanitized = sanitize_copy_text(value);
    auto normalized = value;
    while (!normalized.empty() && std::isspace(static_cast<unsigned char>(normalized.front())) != 0) normalized.remove_prefix(1);
    while (!normalized.empty() && std::isspace(static_cast<unsigned char>(normalized.back())) != 0) normalized.remove_suffix(1);
    return !normalized.empty() && normalized == value && sanitized == value && sanitized.find_first_of("\r\n") == std::string::npos &&
           (!require_relative || !contains_absolute_path(sanitized));
  };
  auto const eligible_query = [&](std::string_view value, bool require_relative) {
    return safe_query(value, require_relative) && tool_card_matches_copy_query(item, value) && tool_action_rows_fit(item, value, width);
  };
  for (auto const& id : {std::string_view(item.call_id), std::string_view(item.request_id), std::string_view(item.correlation_id)})
  {
    if (eligible_query(id, false))
      return std::string(id);
  }
  for (auto const& path : display_changed_paths(item))
  {
    if (eligible_query(path, true))
      return path;
  }
  if (eligible_query(item.name, false))
    return item.name;
  return {};
}

void append_tool_action_lines(std::vector<std::string>& lines, ToolTimelineItem const& item, std::size_t width)
{
  auto const query = stable_tool_query(item, width);
  if (query.empty() || !tool_card_matches_copy_query(item, query) || !tool_action_rows_fit(item, query, width))
    return;
  append_tool_detail_lines(lines, "toggle", "/tool " + query, width);
  append_tool_detail_lines(lines, "copy", "/copy tool " + query, width);
  if (!item.diff.empty())
  {
    append_tool_detail_lines(lines, "diff", "/diff " + query, width);
    append_tool_detail_lines(lines, "copy diff", "/copy diff " + query, width);
  }
}

}  // namespace

std::vector<std::string> render_tool_card(ToolTimelineItem const& item, std::size_t width, bool global_details_visible, bool suppress_result_summary)
{
  std::vector<std::string> lines;
  auto const details_visible = tool_card_details_visible(item, global_details_visible);
  auto const marker = status_marker(item.status);
  auto name_raw = sanitize_terminal_text(item.name.empty() ? "unknown" : item.name);
  auto const is_shell = shell_tool(item);
  auto const command = command_text(item);
  auto const output = details_visible ? output_preview_text(item) : std::optional<std::string>{};
  auto const display_output = output ? std::optional(project_display_path_aliases(item, *output)) : std::nullopt;
  auto const shell_status = is_shell ? exit_status_text(item) : std::optional<std::string>{};
  auto const duration = duration_text(item);
  auto const permission = permission_summary(item);
  auto const truncation = truncation_summary(item);
  auto primary = tool_primary_summary(item, suppress_result_summary, is_shell, command, shell_status, duration, truncation);
  if (!permission.empty())
    primary = primary.empty() ? permission : permission + " · " + primary;

  auto const prefix_text = wide_blocks(width) ? std::string("  │ ") : std::string("  ");
  auto const complete_primary_raw = sanitize_terminal_text(primary);
  auto primary_raw = complete_primary_raw;
  auto const marker_prefix_columns = terminal_text_columns(prefix_text + marker + " ");
  auto const primary_separator_columns = primary_raw.empty() ? std::size_t{0} : terminal_text_columns(" · ");
  auto const full_columns = marker_prefix_columns + terminal_text_columns(name_raw) + primary_separator_columns + terminal_text_columns(primary_raw);
  if (full_columns > width)
  {
    auto const fixed_columns = marker_prefix_columns + terminal_text_columns(name_raw);
    if (!primary_raw.empty() && width > fixed_columns + primary_separator_columns)
      primary_raw = fit_line(std::move(primary_raw), width - fixed_columns - primary_separator_columns);
    else
      primary_raw.clear();
    if (fixed_columns > width && width > marker_prefix_columns)
      name_raw = fit_line(std::move(name_raw), width - marker_prefix_columns);
  }

  std::string line1 = prefix_text + std::string(status_sgr(item.status)) + marker + std::string(kSgrReset) + " " + std::string(kSgrBold) +
                      std::string(kSgrAccent) + name_raw + std::string(kSgrReset);
  if (!primary_raw.empty())
    line1 += " " + std::string(kSgrDim) + "· " + primary_raw + std::string(kSgrReset);
  lines.push_back(fit_line_preserving_sgr(std::move(line1), width));
  auto const primary_was_rendered_completely = !primary_raw.empty() && primary_raw == complete_primary_raw;
  auto const detail_repeats_rendered_primary = [&](std::string const& detail) {
    return primary_was_rendered_completely && detail_repeats_primary(detail, complete_primary_raw);
  };

  if (details_visible)
  {
    if (!permission_audit_payload(item, item.argument_summary) && !detail_repeats_rendered_primary(item.argument_summary))
      append_tool_detail_lines(lines, "args", item.argument_summary, width);
    if (auto input = input_preview_text(item, command))
      append_tool_detail_lines(lines, "input", *input, width);
    append_tool_detail_lines(lines, "call id", item.call_id, width);
    append_tool_detail_lines(lines, "request id", item.request_id, width);
    append_tool_detail_lines(lines, "correlation id", item.correlation_id, width);
    append_tool_detail_lines(lines, "changed", changed_paths_summary(item, true), width);
    if (is_shell && command && !permission_audit_payload(item, *command) && !detail_repeats_rendered_primary(*command))
      append_tool_detail_lines(lines, "command", *command, width);
    if (is_shell && shell_status && !permission_audit_payload(item, *shell_status) && !detail_repeats_rendered_primary(*shell_status))
      append_tool_detail_lines(lines, "status", *shell_status, width);
    if (duration && !detail_repeats_rendered_primary(*duration))
      append_tool_detail_lines(lines, "duration", *duration, width);
    if (is_shell && item.status == ToolTimelineStatus::Running && !detail_repeats_rendered_primary("Esc or Ctrl+C requests stop"))
      append_tool_detail_lines(lines, "cancel", "Esc or Ctrl+C requests stop", width);
    if (is_shell && item.status == ToolTimelineStatus::Canceled && !detail_repeats_rendered_primary("stopped"))
      append_tool_detail_lines(lines, "cancel", "stopped", width);
    append_permission_lines(lines, item, width);
    if (!suppress_result_summary && !permission_audit_payload(item, item.result_summary) &&
        (item.truncated || !detail_repeats_rendered_primary(item.result_summary)) &&
        (item.truncated || !shell_status || !same_payload(item.result_summary, *shell_status)))
      append_tool_detail_lines(lines, "result", project_display_path_aliases(item, item.result_summary), width);
    if (output && display_output && !permission_audit_payload(item, *output) && (item.truncated || !detail_repeats_rendered_primary(*output)) &&
        (item.truncated || (!same_payload(*output, item.result_summary) && (!shell_status || !same_payload(*output, *shell_status)))))
      append_output_preview_lines(lines, item, "output", *display_output, width, kExpandedOutputPreviewLines);
    if (!truncation.empty() && !detail_repeats_rendered_primary(truncation))
      append_tool_detail_lines(lines, "truncation", truncation, width);
    auto const byte_retention = is_shell ? byte_retention_summary(item) : std::string{};
    if (!byte_retention.empty() && !same_payload(byte_retention, truncation))
      append_tool_detail_lines(lines, "bytes", byte_retention, width);
    if (!item.spill_path.empty())
      append_tool_detail_lines(lines, "full output", item.spill_path, width);
    if (item.spill_truncated)
      append_tool_detail_lines(lines, "spill incomplete", "true", width);
    append_diff_lines(lines, item, width);
    append_tool_action_lines(lines, item, width);
  }

  return lines;
}

}  // namespace detail
}  // namespace ava::tui
