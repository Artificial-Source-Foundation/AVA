#include "ava/tui/composer_internal.h"
#include "ava/config/model_profiles.h"
#include "ava/config/provider_profiles.h"
#include "ava/tui/composer_editor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <utility>

namespace ava::tui::detail {
namespace {

std::string composer_bar()
{
  return std::string(kSgrAccent) + std::string(kComposerBar) + std::string(kSgrReset) + std::string(kSgrComposerBg) + "  ";
}

std::string composer_mode_badge(std::string_view mode)
{
  std::string mode_text = sanitize_terminal_text(mode);
  if (!mode_text.empty())
    mode_text.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(mode_text.front())));
  auto const color = mode == "build" || mode == "code" ? kSgrWarning : kSgrAccent;
  return std::string(color) + mode_text + std::string(kSgrReset) + std::string(kSgrComposerBg);
}

std::string provider_label(std::string_view provider)
{
  return sanitize_terminal_text(ava::config::provider_display_name(provider));
}

std::string model_label(std::string_view model)
{
  return sanitize_terminal_text(ava::config::model_display_label(model));
}

std::string compact_path_leaf(std::string_view path)
{
  auto end = path.size();
  while (end > 1 && (path[end - 1] == '/' || path[end - 1] == '\\')) --end;
  auto const trimmed = path.substr(0, end);
  auto const slash = trimmed.find_last_of("/\\");
  if (slash == std::string_view::npos || slash + 1 >= trimmed.size())
    return std::string(trimmed);
  return std::string(trimmed.substr(slash + 1));
}

void append_muted_segment(std::string& line, std::string_view label, std::string_view value)
{
  if (value.empty())
    return;
  line += " " + std::string(kSgrMuted) + "· " + std::string(label) + " " + sanitize_terminal_text(value) + std::string(kSgrReset) + std::string(kSgrComposerBg);
}

std::vector<std::size_t> input_line_start_offsets(std::string_view input)
{
  std::vector<std::size_t> starts{0};
  for (std::size_t index = 0; index < input.size(); ++index)
  {
    if (input[index] != '\n' && input[index] != '\r')
      continue;
    if (input[index] == '\r' && index + 1 < input.size() && input[index + 1] == '\n')
      ++index;
    starts.push_back(index + 1);
  }
  return starts;
}

std::optional<std::pair<std::size_t, std::size_t>> input_selection_bounds(ComposerSnapshot const& snapshot)
{
  if (snapshot.input_selection_start == std::string::npos || snapshot.input_selection_end == std::string::npos)
    return std::nullopt;
  auto start = clamp_composer_draft_cursor(snapshot.input, snapshot.input_selection_start);
  auto end = clamp_composer_draft_cursor(snapshot.input, snapshot.input_selection_end);
  if (end < start)
    std::swap(start, end);
  if (start == end)
    return std::nullopt;
  return std::pair{start, end};
}

struct InputTextCell
{
  std::size_t bytes = 1;
  std::size_t columns = 1;
  bool space = false;
};

InputTextCell input_text_cell(std::string_view text, std::size_t index)
{
  if (index >= text.size())
    return {};
  auto const byte = static_cast<unsigned char>(text[index]);
  if (byte == '\t')
    return {.bytes = 1, .columns = 2, .space = true};
  if (byte < 0x20 || byte == 0x7F)
    return {.bytes = 1, .columns = 1, .space = false};
  if ((byte & 0x80U) == 0)
    return {.bytes = 1, .columns = 1, .space = std::isspace(byte) != 0};

  auto const cell = terminal_text_cell(text, index);
  if (cell.valid)
    return {.bytes = cell.bytes, .columns = cell.columns, .space = false};
  return {.bytes = 1, .columns = 1, .space = false};
}

std::size_t input_content_width(std::size_t width, bool first_line)
{
  auto const prefix_columns = composer_input_prefix_columns(first_line);
  if (width <= prefix_columns)
    return 1;
  return std::max<std::size_t>(1, width - prefix_columns);
}

ComposerInputRenderLine make_input_render_line(std::string_view line, std::size_t logical_start,
                                               std::size_t start, std::size_t end, bool first_line)
{
  return ComposerInputRenderLine{.text = std::string(line.substr(start, end - start)),
                                 .start = logical_start + start,
                                 .end = logical_start + end,
                                 .first_line = first_line};
}

void append_wrapped_input_line(std::vector<ComposerInputRenderLine>& output, std::string_view line,
                               std::size_t logical_start, std::size_t width, bool prompt_line)
{
  if (line.empty())
  {
    output.push_back(ComposerInputRenderLine{.text = {}, .start = logical_start, .end = logical_start, .first_line = prompt_line});
    return;
  }

  auto start = std::size_t{0};
  auto first_visual_line = prompt_line;
  while (start < line.size())
  {
    auto const content_width = input_content_width(width, first_visual_line);
    auto index = start;
    auto columns = std::size_t{0};
    auto last_space_end = std::optional<std::size_t>{};
    while (index < line.size())
    {
      auto const cell = input_text_cell(line, index);
      if (cell.bytes == 0)
        break;
      if (columns + cell.columns > content_width)
        break;
      index += cell.bytes;
      columns += cell.columns;
      if (cell.space)
        last_space_end = index;
    }

    if (index >= line.size())
    {
      output.push_back(make_input_render_line(line, logical_start, start, line.size(), first_visual_line));
      break;
    }

    auto end = index;
    if (last_space_end && *last_space_end > start)
      end = *last_space_end;
    if (end <= start)
    {
      auto const cell = input_text_cell(line, start);
      end = start + std::max<std::size_t>(cell.bytes, 1);
    }
    output.push_back(make_input_render_line(line, logical_start, start, std::min(end, line.size()), first_visual_line));
    start = std::min(end, line.size());
    first_visual_line = false;
  }
}

void append_input_text_segment(std::string& line, std::string_view text, bool selected)
{
  if (text.empty())
    return;
  line += std::string(kSgrText);
  if (selected)
    line += std::string(kReverseVideo);
  line += sanitize_terminal_text(text) + std::string(kSgrReset) + std::string(kSgrComposerBg);
}

void append_input_text(std::string& line, ComposerSnapshot const& snapshot, std::string_view text,
                       std::size_t line_start)
{
  auto const selection = input_selection_bounds(snapshot);
  if (!selection)
  {
    append_input_text_segment(line, text, false);
    return;
  }

  auto const line_end = line_start + text.size();
  auto const selection_start = std::max(selection->first, line_start);
  auto const selection_end = std::min(selection->second, line_end);
  if (selection_start >= selection_end)
  {
    append_input_text_segment(line, text, false);
    return;
  }

  auto const selected_start = selection_start - line_start;
  auto const selected_end = selection_end - line_start;
  append_input_text_segment(line, text.substr(0, selected_start), false);
  append_input_text_segment(line, text.substr(selected_start, selected_end - selected_start), true);
  append_input_text_segment(line, text.substr(selected_end), false);
}

std::string render_status_line(ComposerSnapshot const& snapshot, std::size_t width)
{
  auto const provider = provider_label(snapshot.provider);
  auto const model = model_label(snapshot.model);

  std::string line = composer_bar() + composer_mode_badge(snapshot.mode);
  if (!model.empty() || !provider.empty())
  {
    line += " " + std::string(kSgrMuted) + "·" + std::string(kSgrReset) + std::string(kSgrComposerBg) + " ";
    if (!model.empty())
    {
      line += std::string(kSgrBold) + std::string(kSgrText) + model + std::string(kSgrReset) + std::string(kSgrComposerBg);
    }
    if (!provider.empty())
    {
      if (!model.empty())
        line += " ";
      line += std::string(kSgrMuted) + provider + std::string(kSgrReset) + std::string(kSgrComposerBg);
    }
  }
  if (snapshot.reasoning_status && !snapshot.reasoning_status->empty())
  {
    line += " " + std::string(kSgrMuted) + "·" + std::string(kSgrReset) + std::string(kSgrComposerBg) + " " + std::string(kSgrWarning) +
            sanitize_terminal_text(*snapshot.reasoning_status) + std::string(kSgrReset) + std::string(kSgrComposerBg);
  }
  if (snapshot.sidebar)
  {
    if (!snapshot.sidebar->workspace.empty())
      append_muted_segment(line, "cwd", compact_path_leaf(snapshot.sidebar->workspace));
    if (!snapshot.sidebar->git_branch.empty())
      append_muted_segment(line, "git", snapshot.sidebar->git_branch);
    if (snapshot.sidebar->context_source_count)
    {
      append_muted_segment(line, "ctx", std::to_string(*snapshot.sidebar->context_source_count));
    }
    if (snapshot.sidebar->session_entry_count)
    {
      append_muted_segment(line, "entries", std::to_string(*snapshot.sidebar->session_entry_count));
    }
  }
  std::string right;
  if (snapshot.token_status && !snapshot.token_status->empty())
  {
    if (!right.empty())
      right += "  ";
    right += std::string(kSgrMuted) + sanitize_terminal_text(*snapshot.token_status) + std::string(kSgrReset) + std::string(kSgrComposerBg);
  }
  if (snapshot.processing)
  {
    static constexpr std::array<std::string_view, 10> kSpinner = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    if (!right.empty())
      right += "  ";
    right += std::string(kSgrWarning) + std::string(kSpinner[snapshot.spinner_frame % kSpinner.size()]) + std::string(kSgrReset) + std::string(kSgrComposerBg);
  }

  if (!right.empty())
  {
    constexpr auto kRightGap = std::size_t{2};
    constexpr auto kRightMargin = std::size_t{2};
    auto const left_columns = terminal_text_columns(line);
    auto const right_columns = terminal_text_columns(right);
    if (left_columns + right_columns + kRightGap + kRightMargin <= width)
    {
      line += std::string(width - left_columns - right_columns - kRightMargin, ' ');
      line += right;
      line += std::string(kRightMargin, ' ');
    }
    else
    {
      line += "  " + right;
    }
  }

  return composer_surface_line(std::move(line), width);
}

std::string render_input_line(ComposerSnapshot const& snapshot, std::size_t width)
{
  std::string line = composer_bar() + std::string(kSgrBold) + std::string(kSgrAccent) + std::string(kComposerPrompt) + std::string(kSgrReset) +
                     std::string(kSgrComposerBg) + " ";
  if (snapshot.input.empty())
  {
    line += std::string(kSgrTextDimmed) + "Type a message..." + std::string(kSgrReset) + std::string(kSgrComposerBg);
  }
  else
  {
    append_input_text(line, snapshot, snapshot.input, 0);
  }
  return composer_surface_line(std::move(line), width);
}

std::string render_input_fragment_line(ComposerSnapshot const& snapshot, std::string_view text, bool first_line,
                                       std::size_t width, std::size_t line_start)
{
  std::string line = composer_bar();
  if (first_line)
  {
    line += std::string(kSgrBold) + std::string(kSgrAccent) + std::string(kComposerPrompt) + std::string(kSgrReset) + std::string(kSgrComposerBg) + " ";
  }
  else
  {
    line += "  ";
  }
  append_input_text(line, snapshot, text, line_start);
  return composer_surface_line(std::move(line), width);
}

std::size_t effective_input_cursor(ComposerSnapshot const& snapshot)
{
  if (snapshot.input_cursor == std::string::npos)
    return snapshot.input.size();
  return std::min(snapshot.input_cursor, snapshot.input.size());
}

}  // namespace

std::size_t composer_input_prefix_columns(bool first_line)
{
  auto prefix = std::string(kComposerBar) + "  ";
  prefix += first_line ? std::string(kComposerPrompt) + " " : "  ";
  return terminal_text_columns(sanitize_terminal_text(prefix));
}

std::vector<std::string> input_render_lines(std::string_view input)
{
  auto lines = split_lines(input);
  if (lines.empty())
    lines.push_back("");
  return lines;
}

std::vector<ComposerInputRenderLine> input_render_line_spans(std::string_view input, std::size_t width)
{
  auto logical_lines = split_lines(input);
  if (logical_lines.empty())
    logical_lines.push_back("");
  auto line_starts = input_line_start_offsets(input);
  std::vector<ComposerInputRenderLine> output;
  for (std::size_t index = 0; index < logical_lines.size(); ++index)
  {
    auto const line_start = index < line_starts.size() ? line_starts[index] : input.size();
    append_wrapped_input_line(output, logical_lines[index], line_start, width, index == 0);
  }
  if (output.empty())
    output.push_back(ComposerInputRenderLine{.text = {}, .start = 0, .end = 0, .first_line = true});
  return output;
}

std::size_t composer_block_line_count(ComposerSnapshot const& snapshot, std::size_t height)
{
  return composer_block_line_count(snapshot, height, std::max<std::size_t>(kMinWidth, snapshot.width));
}

std::size_t composer_block_line_count(ComposerSnapshot const& snapshot, std::size_t height, std::size_t width)
{
  auto const input_lines = snapshot.input.empty() ? std::size_t{1} : input_render_line_spans(snapshot.input, width).size();
  auto const desired = std::clamp(input_lines + 2, kMinComposerBlockLines, kMaxComposerBlockLines);
  return std::min(height, desired);
}

ComposerInputLayout composer_input_layout(std::size_t input_line_count, std::size_t max_lines, std::size_t draft_scroll_offset)
{
  auto const effective_input_lines = std::max<std::size_t>(input_line_count, 1);
  if (max_lines <= 1)
  {
    auto const visible_input_lines = std::size_t{1};
    auto const max_scroll = effective_input_lines > visible_input_lines ? effective_input_lines - visible_input_lines : 0;
    auto const scroll = std::min(draft_scroll_offset, max_scroll);
    auto const first_visible = effective_input_lines > visible_input_lines ? effective_input_lines - visible_input_lines - scroll : 0;
    return {.top_padding = 0,
            .first_visible = first_visible,
            .visible_input_lines = visible_input_lines,
            .hidden_above = first_visible,
            .hidden_below = effective_input_lines - first_visible - visible_input_lines};
  }
  auto const input_budget = max_lines - 1;
  auto const visible_input_lines = std::min(effective_input_lines, input_budget);
  auto const max_scroll = effective_input_lines > visible_input_lines ? effective_input_lines - visible_input_lines : 0;
  auto const scroll = std::min(draft_scroll_offset, max_scroll);
  auto const first_visible = effective_input_lines > visible_input_lines ? effective_input_lines - visible_input_lines - scroll : 0;
  auto const content_lines = visible_input_lines + 1;
  auto const padding = max_lines > content_lines ? max_lines - content_lines : 0;
  return {.top_padding = padding / 2,
          .first_visible = first_visible,
          .visible_input_lines = visible_input_lines,
          .hidden_above = first_visible,
          .hidden_below = effective_input_lines - first_visible - visible_input_lines};
}

std::vector<std::string> render_composer_block(ComposerSnapshot const& snapshot, std::size_t width, std::size_t max_lines)
{
  if (max_lines == 0)
    return {};
  if (snapshot.input.empty())
  {
    if (max_lines == 1)
      return {render_input_line(snapshot, width)};
    if (max_lines == 2)
      return {render_input_line(snapshot, width), render_status_line(snapshot, width)};
    std::vector<std::string> lines;
    auto const layout = composer_input_layout(1, max_lines, 0);
    while (lines.size() < layout.top_padding)
    {
      lines.push_back(composer_surface_line("", width));
    }
    lines.push_back(render_input_line(snapshot, width));
    lines.push_back(render_status_line(snapshot, width));
    while (lines.size() < max_lines)
    {
      lines.push_back(composer_surface_line("", width));
    }
    return lines;
  }

  std::vector<std::string> lines;
  auto const input_lines = input_render_line_spans(snapshot.input, width);
  auto const layout = composer_input_layout(input_lines.size(), max_lines, snapshot.draft_scroll_offset);
  while (lines.size() < layout.top_padding)
  {
    lines.push_back(composer_surface_line("", width));
  }
  auto const last_visible = std::min(input_lines.size(), layout.first_visible + layout.visible_input_lines);
  for (std::size_t index = layout.first_visible; index < last_visible; ++index)
  {
    auto const& input_line = input_lines[index];
    lines.push_back(
        render_input_fragment_line(snapshot, input_line.text, input_line.first_line, width, input_line.start));
  }
  if (max_lines > 1)
    lines.push_back(render_status_line(snapshot, width));
  while (lines.size() < max_lines)
  {
    lines.push_back(composer_surface_line("", width));
  }
  return lines;
}

std::size_t input_cursor_column(ComposerSnapshot const& snapshot, std::size_t width)
{
  auto const cursor = effective_input_cursor(snapshot);
  auto const input_lines = input_render_line_spans(snapshot.input, width);
  auto const visual_line = input_cursor_line(snapshot, width);
  auto const& line = input_lines[std::min(visual_line, input_lines.size() - 1)];
  auto const line_cursor = std::clamp(cursor, line.start, line.end);
  auto const line_before_cursor = snapshot.input.substr(line.start, line_cursor - line.start);
  auto column = composer_input_prefix_columns(line.first_line) +
                terminal_text_columns(sanitize_terminal_text(line_before_cursor)) + 1;
  if (column == 0)
    column = 1;
  return std::min(column, std::max<std::size_t>(width, 1));
}

std::size_t input_cursor_line(ComposerSnapshot const& snapshot, std::size_t width)
{
  auto const cursor = effective_input_cursor(snapshot);
  auto const input_lines = input_render_line_spans(snapshot.input, width);
  if (input_lines.empty())
    return 0;
  for (std::size_t index = 0; index < input_lines.size(); ++index)
  {
    auto const& line = input_lines[index];
    if (cursor < line.end)
      return index;
    if (cursor == line.end)
    {
      if (index + 1 >= input_lines.size() || input_lines[index + 1].start != cursor)
        return index;
    }
  }
  return input_lines.size() - 1;
}

}  // namespace ava::tui::detail
