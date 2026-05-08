#include "ava/tui/composer_internal.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tui::detail {
namespace {

struct DiffLineNumbers {
  std::optional<std::size_t> old_line = std::nullopt;
  std::optional<std::size_t> new_line = std::nullopt;
};

std::optional<std::size_t> parse_size(std::string_view text, std::size_t& offset)
{
  auto const start = offset;
  while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') ++offset;
  if (start == offset) return std::nullopt;
  std::size_t value = 0;
  auto const result = std::from_chars(text.data() + start, text.data() + offset, value);
  if (result.ec != std::errc()) return std::nullopt;
  return value;
}

void skip_hunk_count(std::string_view text, std::size_t& offset)
{
  if (offset >= text.size() || text[offset] != ',') return;
  ++offset;
  static_cast<void>(parse_size(text, offset));
}

bool parse_hunk_header(std::string_view line, DiffLineNumbers& numbers)
{
  if (!line.starts_with("@@ -")) return false;
  auto offset = std::size_t{4};
  auto old_line = parse_size(line, offset);
  if (!old_line) return false;
  skip_hunk_count(line, offset);
  if (offset >= line.size() || line[offset] != ' ') return false;
  ++offset;
  if (offset >= line.size() || line[offset] != '+') return false;
  ++offset;
  auto new_line = parse_size(line, offset);
  if (!new_line) return false;

  numbers.old_line = *old_line;
  numbers.new_line = *new_line;
  return true;
}

std::string number_cell(std::optional<std::size_t> value)
{
  constexpr auto kWidth = std::size_t{4};
  if (!value) return std::string(kWidth, ' ');
  auto text = std::to_string(*value);
  if (text.size() >= kWidth) return text;
  return std::string(kWidth - text.size(), ' ') + text;
}

std::string diff_markup_line(std::string_view raw_line, DiffLineNumbers& numbers)
{
  auto const sanitized = sanitize_terminal_text(raw_line);
  if (sanitized.starts_with("@@")) {
    static_cast<void>(parse_hunk_header(sanitized, numbers));
    return std::string(kSgrWarning) + "hunk " + sanitized + std::string(kSgrReset);
  }
  if (sanitized.starts_with("---") || sanitized.starts_with("+++")) {
    return std::string(kSgrMuted) + sanitized + std::string(kSgrReset);
  }

  auto old_display = numbers.old_line;
  auto new_display = numbers.new_line;
  std::string_view sgr = kSgrMuted;
  auto marker = char{' '};
  auto content = std::string_view(sanitized);

  if (!sanitized.empty() && sanitized.front() == '+') {
    marker = '+';
    content = std::string_view(sanitized).substr(1);
    old_display = std::nullopt;
    sgr = kSgrSuccess;
    if (numbers.new_line) ++(*numbers.new_line);
  } else if (!sanitized.empty() && sanitized.front() == '-') {
    marker = '-';
    content = std::string_view(sanitized).substr(1);
    new_display = std::nullopt;
    sgr = kSgrError;
    if (numbers.old_line) ++(*numbers.old_line);
  } else if (!sanitized.empty() && sanitized.front() == ' ') {
    content = std::string_view(sanitized).substr(1);
    if (numbers.old_line) ++(*numbers.old_line);
    if (numbers.new_line) ++(*numbers.new_line);
  } else if (sanitized.starts_with("\\")) {
    old_display = std::nullopt;
    new_display = std::nullopt;
    content = sanitized;
  } else {
    old_display = std::nullopt;
    new_display = std::nullopt;
    content = sanitized;
  }

  return std::string(kSgrMuted) + number_cell(old_display) + " " + number_cell(new_display) + " " +
         std::string(kSgrReset) + std::string(sgr) + marker + std::string(content) + std::string(kSgrReset);
}

}  // namespace

std::vector<std::string> render_unified_diff_body(std::string_view diff, bool diff_truncated, std::size_t width,
                                                  std::string_view line_prefix, std::size_t max_lines)
{
  std::vector<std::string> lines;
  if (diff.empty() || max_lines == 0) return lines;

  auto const raw_lines = split_lines(diff);
  auto const needs_marker = diff_truncated || raw_lines.size() > max_lines;
  auto const body_budget = needs_marker && max_lines > 0 ? max_lines - 1 : max_lines;
  auto const visible_count = std::min(raw_lines.size(), body_budget);
  DiffLineNumbers numbers;
  lines.reserve(visible_count + (needs_marker ? 1U : 0U));

  for (std::size_t index = 0; index < visible_count; ++index) {
    auto line = std::string(line_prefix) + diff_markup_line(raw_lines[index], numbers);
    lines.push_back(fit_line_preserving_sgr(std::move(line), width));
  }

  auto const hidden = raw_lines.size() > visible_count ? raw_lines.size() - visible_count : std::size_t{0};
  if ((hidden > 0 || diff_truncated) && lines.size() < max_lines) {
    std::string marker;
    if (hidden > 0) marker = "… " + std::to_string(hidden) + " diff lines hidden";
    if (diff_truncated) {
      if (!marker.empty()) marker += "; ";
      marker += "[diff truncated]";
    }
    lines.push_back(fit_line_preserving_sgr(
        std::string(line_prefix) + std::string(kSgrWarning) + marker + std::string(kSgrReset), width));
  }

  return lines;
}

}  // namespace ava::tui::detail
