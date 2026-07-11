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
constexpr auto kCollapsedOutputPreviewLines = std::size_t{2};
constexpr auto kExpandedOutputPreviewLines = std::size_t{8};
constexpr auto kExpandedDiffPreviewLines = std::size_t{14};
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
  for (auto scan = index; scan < text.size(); ++scan) {
    auto const byte = static_cast<unsigned char>(text[scan]);
    if (byte >= 0x40 && byte <= 0x7E) return scan + 1;
  }
  return std::nullopt;
}

std::optional<std::size_t> terminal_control_sequence_end(std::string_view text, std::size_t index)
{
  auto const byte = static_cast<unsigned char>(text[index]);
  if (byte != 0x1B || index + 1 >= text.size()) return std::nullopt;

  auto const introducer = text[index + 1];
  if (introducer == '[') return csi_sequence_end(text, index + 2);
  if (introducer != ']') return std::nullopt;

  for (auto scan = index + 2; scan < text.size(); ++scan) {
    auto const current = static_cast<unsigned char>(text[scan]);
    if (current == '\a') return scan + 1;
    if (current == 0x1B && scan + 1 < text.size() && text[scan + 1] == '\\') return scan + 2;
  }
  return std::nullopt;
}

std::string sanitize_copy_text(std::string_view text)
{
  std::string stripped;
  stripped.reserve(text.size());
  for (std::size_t index = 0; index < text.size();) {
    if (auto end = terminal_control_sequence_end(text, index)) {
      index = *end;
      continue;
    }
    stripped.push_back(text[index]);
    ++index;
  }
  return sanitize_terminal_text(stripped);
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

struct OutputPreviewLines
{
  std::vector<std::string_view> visible = {};
  bool has_more = false;
  std::optional<std::size_t> exact_total_lines = std::nullopt;
};

OutputPreviewLines output_preview_lines(std::string_view text, std::size_t max_visible)
{
  OutputPreviewLines preview;
  if (max_visible == 0) return preview;
  auto const count_exact = text.size() <= kExactOutputPreviewLineCountMaxBytes;
  preview.visible.reserve(max_visible);
  std::size_t total_lines = 0;
  std::size_t start = 0;
  for (std::size_t index = 0; index <= text.size(); ++index)
  {
    if (index != text.size() && text[index] != '\n' && text[index] != '\r') continue;

    ++total_lines;
    if (preview.visible.size() < max_visible)
    {
      preview.visible.push_back(text.substr(start, index - start));
    }
    else
    {
      preview.has_more = true;
      if (!count_exact) return preview;
    }

    if (index == text.size()) break;
    if (text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n') ++index;
    start = index + 1;
  }
  if (preview.visible.empty()) preview.visible.push_back({});
  if (count_exact) preview.exact_total_lines = total_lines;
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
    if (path.empty()) return;
    if (std::ranges::find(paths, path) == paths.end()) paths.push_back(std::move(path));
  };
  for (auto const& path : ava::core::json::strings_in_array_field(item.result_json, "changed_paths"))
    append_path(path);
  for (auto const& path : ava::core::json::strings_in_array_field(item.result_json, "changed_files"))
    append_path(path);
  for (auto const& edit : ava::core::json::objects_in_array_field(item.result_json, "edits"))
    append_path(ava::core::json::string_field(edit, "path").value_or(""));
  if (item.name == "write" || item.name == "write_file" || item.name == "edit_file" || item.name == "apply_patch")
  {
    if (auto path = first_json_string(item.result_json, {"path", "target_path"})) append_path(*path);
    if (auto path = first_json_string(item.arguments_json, {"path", "target_path"})) append_path(*path);
  }

  return paths;
}

std::string changed_paths_summary(ToolTimelineItem const& item)
{
  auto const paths = display_changed_paths(item);
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
  auto const paths = display_changed_paths(item);
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

std::string permission_decision_label(ToolPermissionAuditItem const& audit)
{
  auto const decision = lower_ascii(audit.decision);
  if (decision == "allow" || decision == "allowed")
    return "allow";
  if (decision == "allow_session_grant" || decision == "session_grant")
    return "allow session";
  if (decision == "deny" || decision == "denied")
    return "deny";
  return "checked";
}

bool permission_decision_denied(ToolPermissionAuditItem const& audit)
{
  return permission_decision_label(audit) == "deny";
}

std::string permission_audit_detail(ToolPermissionAuditItem const& audit)
{
  std::vector<std::string> parts;
  parts.push_back(permission_decision_label(audit));
  if (!audit.risk.empty()) parts.push_back("risk " + audit.risk);
  if (!audit.permission_request_id.empty()) parts.push_back("id " + audit.permission_request_id);
  if (!audit.resolver_request_id.empty()) parts.push_back("resolver " + audit.resolver_request_id);
  if (!audit.reason.empty()) parts.push_back("reason " + audit.reason);
  if (!audit.resolution_reason.empty()) parts.push_back("resolution " + audit.resolution_reason);
  if (!audit.operation.empty()) parts.push_back("operation " + audit.operation);
  if (!audit.tool_name.empty()) parts.push_back("tool " + audit.tool_name);
  if (!audit.target.empty()) parts.push_back("target " + audit.target);
  if (!audit.command.empty()) parts.push_back("command " + audit.command);

  std::string detail;
  for (std::size_t index = 0; index < parts.size(); ++index)
  {
    if (index > 0) detail += " · ";
    detail += parts[index];
  }
  return detail;
}

std::string permission_audit_show_command(ToolPermissionAuditItem const& audit)
{
  if (audit.permission_request_id.empty()) return {};
  return "/permissions audit show " + audit.permission_request_id;
}

std::string permission_diagnose_command(ToolPermissionAuditItem const& audit)
{
  if (audit.permission_request_id.empty() || !permission_decision_denied(audit)) return {};
  return "/permissions diagnose " + audit.permission_request_id;
}

std::string permission_ids_summary(std::vector<std::string> const& ids)
{
  if (ids.empty()) return {};
  if (ids.size() == 1) return "permission checked " + ids.front();
  return "permissions checked " + std::to_string(ids.size()) + " requests";
}

std::string permission_summary(ToolTimelineItem const& item)
{
  if (item.permissions.empty()) return permission_ids_summary(item.permission_request_ids);
  if (item.permissions.size() == 1)
  {
    auto const& audit = item.permissions.front();
    auto summary = "permission " + permission_decision_label(audit);
    if (!audit.risk.empty()) summary += " · risk " + audit.risk;
    if (!audit.reason.empty())
      summary += " · reason " + audit.reason;
    else if (!audit.permission_request_id.empty())
      summary += " · id " + audit.permission_request_id;
    return summary;
  }

  bool saw_deny = false;
  bool saw_allow = false;
  std::string risk;
  for (auto const& audit : item.permissions)
  {
    auto const decision = permission_decision_label(audit);
    saw_deny = saw_deny || decision == "deny";
    saw_allow = saw_allow || decision.starts_with("allow");
    if (risk.empty() && !audit.risk.empty()) risk = audit.risk;
  }

  auto summary = "permissions " + std::to_string(item.permissions.size()) + " checked";
  if (saw_deny)
    summary += " · deny";
  else if (saw_allow)
    summary += " · allow";
  if (!risk.empty()) summary += " · risk " + risk;
  return summary;
}

void append_permission_lines(std::vector<std::string>& lines, ToolTimelineItem const& item, std::size_t width)
{
  if (item.permissions.empty())
  {
    auto ids = permission_ids_summary(item.permission_request_ids);
    if (ids.empty()) return;
    if (width >= kWidePermissionDetailWidth)
    {
      append_tool_detail_lines(lines, "permission", ids, width);
      return;
    }
    append_tool_detail_lines(lines, "permission", item.permission_request_ids.size() == 1 ? "checked" : std::to_string(item.permission_request_ids.size()) + " checked", width);
    for (auto const& id : item.permission_request_ids) append_tool_detail_lines(lines, "id", id, width);
    return;
  }
  for (auto const& audit : item.permissions)
  {
    if (width >= kWidePermissionDetailWidth)
    {
      append_tool_detail_lines(lines, "permission", permission_audit_detail(audit), width);
      continue;
    }
    append_tool_detail_lines(lines, "permission", permission_decision_label(audit), width);
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
    if (auto inspect = permission_audit_show_command(audit); !inspect.empty()) append_tool_detail_lines(lines, "inspect", inspect, width);
    if (auto diagnose = permission_diagnose_command(audit); !diagnose.empty()) append_tool_detail_lines(lines, "diagnose", diagnose, width);
  }
}

std::string status_marker(ToolTimelineStatus status)
{
  switch (status)
  {
    case ToolTimelineStatus::Running:
      return "[~]";
    case ToolTimelineStatus::Success:
      return "[+]";
    case ToolTimelineStatus::Canceled:
      return "[-]";
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
  if (text.empty()) return;
  auto const lines = split_lines(text);
  if (lines.empty()) return;
  if (lines.size() == 1) {
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
  if (query.empty()) return true;
  auto const needle = lower_ascii(query);
  auto const matches = [&needle](std::string_view value) {
    return !value.empty() && lower_ascii(value).find(needle) != std::string::npos;
  };
  return matches(permission.permission_request_id) || matches(permission.resolver_request_id) || matches(permission.decision) ||
         matches(permission.operation) || matches(permission.tool_name) || matches(permission.risk) || matches(permission.reason) ||
         matches(permission.target) || matches(permission.command) || matches(permission.resolution_reason) ||
         matches(permission_audit_show_command(permission)) || matches(permission_diagnose_command(permission));
}

bool tool_card_matches_copy_query(ToolTimelineItem const& item, std::string_view query)
{
  if (query.empty()) return true;
  auto const needle = lower_ascii(query);
  auto const matches = [&needle](std::string_view value) {
    return !value.empty() && lower_ascii(value).find(needle) != std::string::npos;
  };

  if (matches(item.name) || matches(item.argument_summary) || matches(item.result_summary) || matches(item.arguments_json) ||
      matches(item.result_json) || matches(item.call_id) || matches(item.request_id) || matches(item.correlation_id) ||
      matches(item.diff) || matches(item.spill_path))
    return true;
  for (auto const& path : display_changed_paths(item))
    if (matches(path)) return true;
  for (auto const& id : item.permission_request_ids)
    if (matches(id)) return true;
  for (auto const& permission : item.permissions) {
    if (permission_matches_copy_query(permission, query)) return true;
  }
  return false;
}

std::string tool_card_diff_copy_text(ToolTimelineItem const& item)
{
  if (item.diff.empty()) return {};
  std::string output;
  for (auto const& line : split_lines(item.diff)) {
    if (!output.empty()) output += '\n';
    output += sanitize_copy_text(line);
  }
  if (item.diff_truncated) {
    if (!output.empty()) output += '\n';
    output += "[diff truncated]";
  }
  return output;
}

std::string tool_card_permission_copy_text(ToolTimelineItem const& item, std::string_view query)
{
  std::string output;
  for (auto const& audit : item.permissions) {
    if (!permission_matches_copy_query(audit, query)) continue;
    append_copy_block(output, "permission", permission_audit_detail(audit));
    append_copy_block(output, "inspect", permission_audit_show_command(audit));
    append_copy_block(output, "diagnose", permission_diagnose_command(audit));
  }
  if (!output.empty() && output.back() == '\n') output.pop_back();
  return output;
}

std::string tool_card_copy_text(ToolTimelineItem const& item)
{
  std::string output;
  append_copy_block(output, "tool", item.name.empty() ? std::string("unknown") : item.name);
  append_copy_block(output, "status", ava::tui::to_string(item.status));
  append_copy_block(output, "lifecycle", ava::tui::to_string(item.lifecycle));
  append_copy_block(output, "args", item.argument_summary);

  auto const is_shell = shell_tool(item);
  if (is_shell) {
    if (auto command = command_text(item)) append_copy_block(output, "command", *command);
    if (auto shell_status = exit_status_text(item)) append_copy_block(output, "shell status", *shell_status);
    if (auto shell_duration = duration_text(item)) append_copy_block(output, "duration", *shell_duration);
  }

  for (auto const& audit : item.permissions) {
    append_copy_block(output, "permission", permission_audit_detail(audit));
    append_copy_block(output, "inspect", permission_audit_show_command(audit));
    append_copy_block(output, "diagnose", permission_diagnose_command(audit));
  }
  append_copy_block(output, "result", item.result_summary);
  if (auto output_preview = output_preview_text(item)) append_copy_block(output, "output", *output_preview);
  auto const truncation = truncation_summary(item);
  append_copy_block(output, "truncation", truncation);
  append_copy_block(output, "changed", changed_paths_summary(item));
  append_copy_block(output, "spill", item.spill_path);
  append_copy_block(output, "diff", item.diff);
  if (!output.empty() && output.back() == '\n') output.pop_back();
  return output;
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
  auto const permission = permission_summary(item);
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
    append_tool_detail_lines(lines, "changed", changed_paths_summary(item), width);
    if (is_shell && command)
      append_tool_detail_lines(lines, "command", *command, width);
    if (is_shell && shell_status)
      append_tool_detail_lines(lines, "status", *shell_status, width);
    if (is_shell && shell_duration)
      append_tool_detail_lines(lines, "duration", *shell_duration, width);
    if (is_shell && item.status == ToolTimelineStatus::Running)
      append_tool_detail_lines(lines, "cancel", "Esc or Ctrl+C requests stop", width);
    if (is_shell && item.status == ToolTimelineStatus::Canceled)
      append_tool_detail_lines(lines, "cancel", "stopped", width);
    append_permission_lines(lines, item, width);
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
      if (item.status == ToolTimelineStatus::Canceled)
        compact += " · stopped";
    }
    if (!compact.empty())
    {
      auto result_raw = sanitize_terminal_text(compact);
      std::string line2 = (wide_blocks(width) ? std::string("  │     ") : std::string("      ")) + std::string(kSgrMuted) + result_raw + std::string(kSgrReset);
      lines.push_back(fit_line_preserving_sgr(line2, width));
    }
    if (!permission.empty())
    {
      auto permission_raw = sanitize_terminal_text(permission);
      std::string permission_line = (wide_blocks(width) ? std::string("  │     ") : std::string("      ")) + std::string(kSgrWarning) +
                                    permission_raw + std::string(kSgrReset);
      lines.push_back(fit_line_preserving_sgr(permission_line, width));
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
