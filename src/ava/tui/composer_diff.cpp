#include "sys.h"
#include "ava/tui/composer_internal.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::tui::detail {
namespace {

// Tight cap on equal-length replacement runs eligible for intra-line emphasis.
// Keeps pairing work linear in the already-bounded diff preview.
constexpr std::size_t kMaxIntralinePairRun = 8;

struct DiffLineNumbers
{
  std::optional<std::size_t> old_line = std::nullopt;
  std::optional<std::size_t> new_line = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Byte spans of a shared prefix/suffix around the changed middle of one side.
struct IntralineSpan
{
  std::size_t prefix_bytes = 0;
  std::size_t middle_bytes = 0;
  bool emphasize = false;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

enum class DiffBodyLineKind
{
  HunkHeader,
  FileHeader,
  Removed,
  Added,
  Context,
  Other,
};

std::optional<std::size_t> parse_size(std::string_view text, std::size_t& offset)
{
  auto const start = offset;
  while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') ++offset;
  if (start == offset)
    return std::nullopt;
  std::size_t value = 0;
  auto const result = std::from_chars(text.data() + start, text.data() + offset, value);
  if (result.ec != std::errc())
    return std::nullopt;
  return value;
}

void skip_hunk_count(std::string_view text, std::size_t& offset)
{
  if (offset >= text.size() || text[offset] != ',')
    return;
  ++offset;
  static_cast<void>(parse_size(text, offset));
}

bool parse_hunk_header(std::string_view line, DiffLineNumbers& numbers)
{
  if (!line.starts_with("@@ -"))
    return false;
  auto offset = std::size_t{4};
  auto old_line = parse_size(line, offset);
  if (!old_line)
    return false;
  skip_hunk_count(line, offset);
  if (offset >= line.size() || line[offset] != ' ')
    return false;
  ++offset;
  if (offset >= line.size() || line[offset] != '+')
    return false;
  ++offset;
  auto new_line = parse_size(line, offset);
  if (!new_line)
    return false;

  numbers.old_line = *old_line;
  numbers.new_line = *new_line;
  return true;
}

std::string number_cell(std::optional<std::size_t> const& value)
{
  constexpr auto kWidth = std::size_t{4};
  if (!value)
    return std::string(kWidth, ' ');
  auto text = std::to_string(*value);
  if (text.size() >= kWidth)
    return text;
  return std::string(kWidth - text.size(), ' ') + text;
}

DiffBodyLineKind classify_diff_body_line(std::string_view sanitized)
{
  if (sanitized.starts_with("@@"))
    return DiffBodyLineKind::HunkHeader;
  if (sanitized.starts_with("---") || sanitized.starts_with("+++"))
    return DiffBodyLineKind::FileHeader;
  if (!sanitized.empty() && sanitized.front() == '-')
    return DiffBodyLineKind::Removed;
  if (!sanitized.empty() && sanitized.front() == '+')
    return DiffBodyLineKind::Added;
  if (!sanitized.empty() && sanitized.front() == ' ')
    return DiffBodyLineKind::Context;
  return DiffBodyLineKind::Other;
}

std::string_view diff_line_content(std::string_view sanitized, DiffBodyLineKind kind)
{
  if (kind == DiffBodyLineKind::Removed || kind == DiffBodyLineKind::Added || kind == DiffBodyLineKind::Context)
  {
    if (!sanitized.empty())
      return sanitized.substr(1);
  }
  return sanitized;
}

// Advance by one compact terminal-text cluster (or one replacement byte for invalid UTF-8).
std::size_t next_cluster_bytes(std::string_view text, std::size_t index)
{
  if (index >= text.size())
    return 0;
  auto const cell = terminal_text_cell(text, index);
  return cell.bytes == 0 ? std::size_t{1} : cell.bytes;
}

std::size_t common_cluster_prefix_bytes(std::string_view left, std::string_view right)
{
  std::size_t index = 0;
  while (index < left.size() && index < right.size())
  {
    auto const left_bytes = next_cluster_bytes(left, index);
    auto const right_bytes = next_cluster_bytes(right, index);
    if (left_bytes == 0 || right_bytes == 0 || left_bytes != right_bytes)
      break;
    if (left.compare(index, left_bytes, right, index, right_bytes) != 0)
      break;
    index += left_bytes;
  }
  return index;
}

std::size_t common_cluster_suffix_bytes(std::string_view left, std::string_view right, std::size_t prefix_bytes)
{
  if (prefix_bytes > left.size() || prefix_bytes > right.size())
    return 0;

  auto const left_rest = left.substr(prefix_bytes);
  auto const right_rest = right.substr(prefix_bytes);
  if (left_rest.empty() || right_rest.empty())
    return 0;

  std::vector<std::size_t> left_starts;
  std::vector<std::size_t> right_starts;
  left_starts.reserve(left_rest.size());
  right_starts.reserve(right_rest.size());

  for (std::size_t index = 0; index < left_rest.size();)
  {
    left_starts.push_back(index);
    index += next_cluster_bytes(left_rest, index);
  }
  for (std::size_t index = 0; index < right_rest.size();)
  {
    right_starts.push_back(index);
    index += next_cluster_bytes(right_rest, index);
  }

  std::size_t matched = 0;
  while (matched < left_starts.size() && matched < right_starts.size())
  {
    auto const left_index = left_starts.size() - 1 - matched;
    auto const right_index = right_starts.size() - 1 - matched;
    auto const left_start = left_starts[left_index];
    auto const right_start = right_starts[right_index];
    auto const left_end = left_index + 1 < left_starts.size() ? left_starts[left_index + 1] : left_rest.size();
    auto const right_end = right_index + 1 < right_starts.size() ? right_starts[right_index + 1] : right_rest.size();
    auto const left_bytes = left_end - left_start;
    auto const right_bytes = right_end - right_start;
    if (left_bytes != right_bytes || left_rest.compare(left_start, left_bytes, right_rest, right_start, right_bytes) != 0)
      break;
    ++matched;
  }
  if (matched == 0)
    return 0;
  auto const suffix_start = left_starts[left_starts.size() - matched];
  return left_rest.size() - suffix_start;
}

bool middle_can_emphasize(std::string_view middle)
{
  // Empty or non-visible middles (e.g. zero-width only) keep whole-line styling.
  return !middle.empty() && terminal_text_columns(middle) > 0;
}

std::pair<IntralineSpan, IntralineSpan> compute_intraline_spans(std::string_view removed_content, std::string_view added_content)
{
  auto const prefix = common_cluster_prefix_bytes(removed_content, added_content);
  auto const suffix = common_cluster_suffix_bytes(removed_content, added_content, prefix);
  if (prefix + suffix > removed_content.size() || prefix + suffix > added_content.size())
    return {};

  auto const removed_middle = removed_content.size() - prefix - suffix;
  auto const added_middle = added_content.size() - prefix - suffix;
  IntralineSpan removed_span{
      .prefix_bytes = prefix, .middle_bytes = removed_middle, .emphasize = middle_can_emphasize(removed_content.substr(prefix, removed_middle))};
  IntralineSpan added_span{.prefix_bytes = prefix, .middle_bytes = added_middle, .emphasize = middle_can_emphasize(added_content.substr(prefix, added_middle))};
  return {removed_span, added_span};
}

std::string emphasize_diff_content(std::string_view content, std::string_view role_sgr, IntralineSpan const& span)
{
  if (!span.emphasize || span.prefix_bytes > content.size())
    return std::string(content);

  auto const prefix_bytes = span.prefix_bytes;
  auto const middle_bytes = std::min(span.middle_bytes, content.size() - prefix_bytes);
  auto const prefix = content.substr(0, prefix_bytes);
  auto const middle = content.substr(prefix_bytes, middle_bytes);
  auto const suffix = content.substr(prefix_bytes + middle_bytes);

  std::string out;
  out.reserve(content.size() + role_sgr.size() * 2 + kSgrBold.size() + kSgrUnderline.size() + kSgrReset.size() * 2);
  out.append(prefix);
  out.append(kSgrBold);
  out.append(kSgrUnderline);
  out.append(middle);
  out.append(kSgrReset);
  out.append(role_sgr);
  out.append(suffix);
  return out;
}

// Pair unambiguous equal-length replacement runs inside each hunk. Values are
// optional emphasis spans keyed by visible body-line index.
void compute_intraline_pair_spans(std::vector<std::string> const& raw_lines, std::size_t visible_count, std::vector<std::optional<IntralineSpan>>& spans)
{
  spans.assign(visible_count, std::nullopt);
  std::size_t index = 0;
  while (index < visible_count)
  {
    auto const kind = classify_diff_body_line(sanitize_terminal_text(raw_lines[index]));
    if (kind == DiffBodyLineKind::HunkHeader || kind == DiffBodyLineKind::FileHeader || kind == DiffBodyLineKind::Context || kind == DiffBodyLineKind::Other)
    {
      ++index;
      continue;
    }
    if (kind != DiffBodyLineKind::Removed)
    {
      // Lone added runs are not replacement pairs.
      ++index;
      continue;
    }

    auto const removed_start = index;
    while (index < visible_count && classify_diff_body_line(sanitize_terminal_text(raw_lines[index])) == DiffBodyLineKind::Removed) ++index;
    auto const removed_count = index - removed_start;

    auto const added_start = index;
    while (index < visible_count && classify_diff_body_line(sanitize_terminal_text(raw_lines[index])) == DiffBodyLineKind::Added) ++index;
    auto const added_count = index - added_start;

    if (removed_count == 0 || removed_count != added_count || removed_count > kMaxIntralinePairRun)
      continue;

    for (std::size_t offset = 0; offset < removed_count; ++offset)
    {
      auto const removed_sanitized = sanitize_terminal_text(raw_lines[removed_start + offset]);
      auto const added_sanitized = sanitize_terminal_text(raw_lines[added_start + offset]);
      auto const removed_content = diff_line_content(removed_sanitized, DiffBodyLineKind::Removed);
      auto const added_content = diff_line_content(added_sanitized, DiffBodyLineKind::Added);
      auto const [removed_span, added_span] = compute_intraline_spans(removed_content, added_content);
      if (removed_span.emphasize)
        spans[removed_start + offset] = removed_span;
      if (added_span.emphasize)
        spans[added_start + offset] = added_span;
    }
  }
}

std::string diff_markup_line(std::string_view raw_line, DiffLineNumbers& numbers, std::optional<IntralineSpan> const& intraline)
{
  auto const sanitized = sanitize_terminal_text(raw_line);
  auto const kind = classify_diff_body_line(sanitized);
  if (kind == DiffBodyLineKind::HunkHeader)
  {
    static_cast<void>(parse_hunk_header(sanitized, numbers));
    return std::string(kSgrWarning) + "hunk " + sanitized + std::string(kSgrReset);
  }
  if (kind == DiffBodyLineKind::FileHeader)
  {
    return std::string(kSgrMuted) + sanitized + std::string(kSgrReset);
  }

  auto old_cell = number_cell(numbers.old_line);
  auto new_cell = number_cell(numbers.new_line);
  std::string_view sgr = kSgrMuted;
  auto marker = char{' '};
  auto content = std::string_view(sanitized);

  if (kind == DiffBodyLineKind::Added)
  {
    marker = '+';
    content = diff_line_content(sanitized, kind);
    old_cell = number_cell(std::nullopt);
    sgr = kSgrSuccess;
    if (numbers.new_line)
      ++(*numbers.new_line);
  }
  else if (kind == DiffBodyLineKind::Removed)
  {
    marker = '-';
    content = diff_line_content(sanitized, kind);
    new_cell = number_cell(std::nullopt);
    sgr = kSgrError;
    if (numbers.old_line)
      ++(*numbers.old_line);
  }
  else if (kind == DiffBodyLineKind::Context)
  {
    content = diff_line_content(sanitized, kind);
    if (numbers.old_line)
      ++(*numbers.old_line);
    if (numbers.new_line)
      ++(*numbers.new_line);
  }
  else
  {
    old_cell = number_cell(std::nullopt);
    new_cell = number_cell(std::nullopt);
    content = sanitized;
  }

  std::string styled_content;
  if (intraline && (kind == DiffBodyLineKind::Added || kind == DiffBodyLineKind::Removed))
    styled_content = emphasize_diff_content(content, sgr, *intraline);
  else
    styled_content = std::string(content);

  return std::string(kSgrMuted) + old_cell + " " + new_cell + " " + std::string(kSgrReset) + std::string(sgr) + marker + std::move(styled_content) +
         std::string(kSgrReset);
}

}  // namespace

std::vector<std::string> render_unified_diff_body(std::string_view diff, bool diff_truncated, std::size_t width, std::string_view line_prefix,
                                                  std::size_t max_lines)
{
  std::vector<std::string> lines;
  if (diff.empty() || max_lines == 0)
    return lines;

  auto const raw_lines = split_lines(diff);
  auto const needs_marker = diff_truncated || raw_lines.size() > max_lines;
  auto const body_budget = needs_marker && max_lines > 0 ? max_lines - 1 : max_lines;
  auto const visible_count = std::min(raw_lines.size(), body_budget);
  DiffLineNumbers numbers;
  lines.reserve(visible_count + (needs_marker ? 1U : 0U));

  std::vector<std::optional<IntralineSpan>> intraline_spans;
  compute_intraline_pair_spans(raw_lines, visible_count, intraline_spans);

  for (std::size_t index = 0; index < visible_count; ++index)
  {
    auto line = std::string(line_prefix) + diff_markup_line(raw_lines[index], numbers, intraline_spans[index]);
    lines.push_back(fit_line_preserving_sgr(std::move(line), width));
  }

  auto const hidden = raw_lines.size() > visible_count ? raw_lines.size() - visible_count : std::size_t{0};
  if ((hidden > 0 || diff_truncated) && lines.size() < max_lines)
  {
    std::string marker;
    if (hidden > 0)
      marker = "… " + std::to_string(hidden) + " diff lines hidden";
    if (diff_truncated)
    {
      if (!marker.empty())
        marker += "; ";
      marker += "[diff truncated]";
    }
    lines.push_back(fit_line_preserving_sgr(std::string(line_prefix) + std::string(kSgrWarning) + marker + std::string(kSgrReset), width));
  }

  return lines;
}

}  // namespace ava::tui::detail
