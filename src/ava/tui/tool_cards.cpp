#include "ava/tui/tool_cards.h"

#include <algorithm>

#include "ava/tui/composer_internal.h"

namespace ava::tui {
namespace {

constexpr auto kBlockMinWidth = std::size_t{32};

bool wide_blocks(std::size_t width)
{
  return width >= kBlockMinWidth;
}

void append_tool_detail_lines(std::vector<std::string>& lines, std::string_view label, std::string const& text,
                              std::size_t width)
{
  if (text.empty()) return;
  auto const prefix = wide_blocks(width) ? std::string("  │     ") : std::string("      ");
  auto const label_prefix =
      prefix + std::string(detail::kSgrDim) + std::string(label) + ": " + std::string(detail::kSgrReset);
  for (auto const& raw_line : split_lines(text)) {
    lines.push_back(detail::fit_line_preserving_sgr(label_prefix + sanitize_terminal_text(raw_line), width));
  }
}

void append_diff_lines(std::vector<std::string>& lines, ToolTimelineItem const& item, std::size_t width)
{
  if (item.diff.empty()) return;
  auto const prefix = wide_blocks(width) ? std::string("  │     ") : std::string("      ");
  lines.push_back(detail::fit_line_preserving_sgr(
      prefix + std::string(detail::kSgrDim) + "diff:" + std::string(detail::kSgrReset), width));
  auto const content_prefix = wide_blocks(width) ? std::string("  │       ") : std::string("        ");
  for (auto const& raw_line : split_lines(item.diff)) {
    auto sanitized = sanitize_terminal_text(raw_line);
    std::string_view sgr = detail::kSgrMuted;
    if (!sanitized.empty() && sanitized.front() == '+') {
      sgr = detail::kSgrSuccess;
    } else if (!sanitized.empty() && sanitized.front() == '-') {
      sgr = detail::kSgrError;
    }
    lines.push_back(detail::fit_line_preserving_sgr(
        content_prefix + std::string(sgr) + std::move(sanitized) + std::string(detail::kSgrReset), width));
  }
  if (item.diff_truncated) {
    lines.push_back(detail::fit_line_preserving_sgr(
        content_prefix + std::string(detail::kSgrWarning) + "[diff truncated]" + std::string(detail::kSgrReset),
        width));
  }
}

std::string truncation_summary(ToolTimelineItem const& item)
{
  if (!item.truncated && item.spill_path.empty() && !item.spill_truncated) return {};

  std::string summary;
  if (item.truncated) {
    if (item.output_bytes && item.total_bytes) {
      summary = "truncated " + std::to_string(*item.output_bytes) + "/" + std::to_string(*item.total_bytes) + " bytes";
    } else if (item.visible_matches && item.total_matches) {
      summary =
          "truncated " + std::to_string(*item.visible_matches) + "/" + std::to_string(*item.total_matches) + " matches";
    } else {
      summary = "truncated output";
    }
    std::vector<std::string> omitted;
    if (item.omitted_bytes) omitted.push_back(std::to_string(*item.omitted_bytes) + " bytes");
    if (item.omitted_lines) omitted.push_back(std::to_string(*item.omitted_lines) + " lines");
    if (!omitted.empty()) {
      summary += "; omitted ";
      for (std::size_t index = 0; index < omitted.size(); ++index) {
        if (index > 0) summary += ", ";
        summary += omitted[index];
      }
    }
  }
  if (!item.spill_path.empty()) {
    if (!summary.empty()) summary += "; ";
    summary += "spill " + item.spill_path;
  }
  if (item.spill_truncated) {
    if (!summary.empty()) summary += "; ";
    summary += "spill truncated";
  }
  return summary;
}

std::string status_marker(ToolTimelineStatus status)
{
  switch (status) {
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
  switch (status) {
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
  switch (state) {
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

  auto prefix_text = wide_blocks(width) ? std::string("  │ ") : std::string("  ");
  auto prefix_cols = terminal_text_columns(prefix_text + marker + " ");
  auto name_cols = terminal_text_columns(name_raw);
  auto lifecycle_cols = terminal_text_columns(std::string("  ") + lifecycle_raw);
  auto args_cols = args_raw.empty() ? 0 : terminal_text_columns(std::string("  ") + args_raw);

  if (prefix_cols + name_cols + lifecycle_cols + args_cols > width) {
    if (!args_raw.empty() && width > prefix_cols + name_cols + lifecycle_cols) {
      auto budget = width - prefix_cols - name_cols - lifecycle_cols;
      args_raw = fit_line(std::move(args_raw), budget);
      args_cols = terminal_text_columns(args_raw);
    }
    if (prefix_cols + name_cols + lifecycle_cols + args_cols > width && width > prefix_cols + lifecycle_cols) {
      auto budget = width - prefix_cols - lifecycle_cols;
      name_raw = fit_line(std::move(name_raw), budget);
    }
  }

  std::string line1 = prefix_text + std::string(status_sgr(item.status)) + marker + std::string(kSgrReset) + " " +
                      std::string(kSgrBold) + std::string(kSgrAccent) + name_raw + std::string(kSgrReset) + "  " +
                      std::string(kSgrMuted) + lifecycle_raw + std::string(kSgrReset);
  if (!args_raw.empty()) {
    line1 += "  " + std::string(kSgrDim) + args_raw + std::string(kSgrReset);
  }
  lines.push_back(fit_line_preserving_sgr(line1, width));

  auto const truncation = truncation_summary(item);
  if (details_visible) {
    append_tool_detail_lines(lines, "args", item.argument_summary, width);
    append_tool_detail_lines(lines, "result", item.result_summary, width);
    if (!truncation.empty()) append_tool_detail_lines(lines, "truncation", truncation, width);
    if (!item.spill_path.empty()) append_tool_detail_lines(lines, "spill", item.spill_path, width);
    append_diff_lines(lines, item, width);
  } else {
    auto const compact = !item.result_summary.empty() ? item.result_summary : truncation;
    if (!compact.empty()) {
      auto result_raw = sanitize_terminal_text(compact);
      std::string line2 = (wide_blocks(width) ? std::string("  │     ") : std::string("      ")) +
                          std::string(kSgrMuted) + result_raw + std::string(kSgrReset);
      lines.push_back(fit_line_preserving_sgr(line2, width));
    }
    if (!item.result_summary.empty() && !truncation.empty()) {
      std::string line3 = (wide_blocks(width) ? std::string("  │     ") : std::string("      ")) +
                          std::string(kSgrWarning) + sanitize_terminal_text(truncation) + std::string(kSgrReset);
      lines.push_back(fit_line_preserving_sgr(line3, width));
    }
  }

  return lines;
}

}  // namespace detail
}  // namespace ava::tui
