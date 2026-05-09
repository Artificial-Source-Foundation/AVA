#include "ava/tui/composer.h"
#include "ava/tui/composer_internal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <vector>
#include <curses.h>

namespace ava::tui {
namespace {

using detail::kSgrAccent;
using detail::kSgrBold;
using detail::kSgrDim;
using detail::kSgrError;
using detail::kSgrReset;
using detail::kSgrSuccess;
using detail::kSgrWarning;

constexpr short kPairText = 1;
constexpr short kPairMuted = 2;
constexpr short kPairSuccess = 3;
constexpr short kPairWarning = 4;
constexpr short kPairError = 5;
constexpr short kPairAccent = 6;
constexpr short kPairScreen = 7;
constexpr short kPairComposer = 8;
constexpr short kPairComposerText = 9;
constexpr short kPairComposerMuted = 10;
constexpr short kPairComposerSuccess = 11;
constexpr short kPairComposerWarning = 12;
constexpr short kPairComposerError = 13;
constexpr short kPairComposerAccent = 14;

constexpr short kColorScreenBg = 16;
constexpr short kColorComposerBg = 17;

enum class ColorRole
{
  Text,
  Muted,
  Success,
  Warning,
  Error,
  Accent,
};

enum class BackgroundRole
{
  Screen,
  Composer,
};

struct CursesStyle
{
  attr_t attributes = A_NORMAL;
  ColorRole color = ColorRole::Text;
  BackgroundRole background = BackgroundRole::Screen;
};

struct CursorPlacement
{
  std::size_t row = 0;
  std::size_t column = 0;
};

void initialize_color_pairs()
{
  static bool initialized = false;
  if (initialized || !has_colors())
    return;
  initialized = true;
  auto screen_bg = COLOR_BLACK;
  auto composer_bg = COLOR_BLACK;
  if (can_change_color() && COLORS > kColorComposerBg)
  {
    static_cast<void>(init_color(kColorScreenBg, 43, 55, 78));
    static_cast<void>(init_color(kColorComposerBg, 102, 122, 180));
    screen_bg = kColorScreenBg;
    composer_bg = kColorComposerBg;
  }
  static_cast<void>(init_pair(kPairText, COLOR_WHITE, screen_bg));
  static_cast<void>(init_pair(kPairMuted, COLOR_CYAN, screen_bg));
  static_cast<void>(init_pair(kPairSuccess, COLOR_GREEN, screen_bg));
  static_cast<void>(init_pair(kPairWarning, COLOR_YELLOW, screen_bg));
  static_cast<void>(init_pair(kPairError, COLOR_RED, screen_bg));
  static_cast<void>(init_pair(kPairAccent, COLOR_CYAN, screen_bg));
  static_cast<void>(init_pair(kPairScreen, COLOR_WHITE, screen_bg));
  static_cast<void>(init_pair(kPairComposer, COLOR_WHITE, composer_bg));
  static_cast<void>(init_pair(kPairComposerText, COLOR_WHITE, composer_bg));
  static_cast<void>(init_pair(kPairComposerMuted, COLOR_CYAN, composer_bg));
  static_cast<void>(init_pair(kPairComposerSuccess, COLOR_GREEN, composer_bg));
  static_cast<void>(init_pair(kPairComposerWarning, COLOR_YELLOW, composer_bg));
  static_cast<void>(init_pair(kPairComposerError, COLOR_RED, composer_bg));
  static_cast<void>(init_pair(kPairComposerAccent, COLOR_CYAN, composer_bg));
}

short color_pair_for(CursesStyle const& style)
{
  if (style.background == BackgroundRole::Composer)
  {
    switch (style.color)
    {
      case ColorRole::Muted:
        return kPairComposerMuted;
      case ColorRole::Success:
        return kPairComposerSuccess;
      case ColorRole::Warning:
        return kPairComposerWarning;
      case ColorRole::Error:
        return kPairComposerError;
      case ColorRole::Accent:
        return kPairComposerAccent;
      case ColorRole::Text:
        return kPairComposerText;
    }
  }

  switch (style.color)
  {
    case ColorRole::Muted:
      return kPairMuted;
    case ColorRole::Success:
      return kPairSuccess;
    case ColorRole::Warning:
      return kPairWarning;
    case ColorRole::Error:
      return kPairError;
    case ColorRole::Accent:
      return kPairAccent;
    case ColorRole::Text:
      return kPairText;
  }
  return kPairText;
}

attr_t curses_attributes(CursesStyle const& style)
{
  if (!has_colors())
    return style.attributes;
  return style.attributes | COLOR_PAIR(color_pair_for(style));
}

bool parse_sgr_codes(std::string_view sequence, std::vector<int>& codes)
{
  codes.clear();
  if (sequence.empty())
    return false;
  std::size_t start = 0;
  while (start <= sequence.size())
  {
    auto const end = sequence.find(';', start);
    auto const token = sequence.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (token.empty())
    {
      codes.push_back(0);
    }
    else
    {
      auto const token_text = std::string(token);
      char* parsed_end = nullptr;
      auto const value = std::strtol(token_text.c_str(), &parsed_end, 10);
      if (parsed_end == nullptr || *parsed_end != '\0')
        return false;
      codes.push_back(static_cast<int>(value));
    }
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  return true;
}

void apply_sgr_codes(std::vector<int> const& codes, CursesStyle& style)
{
  if (codes.empty())
  {
    style = CursesStyle{};
    return;
  }
  for (std::size_t index = 0; index < codes.size(); ++index)
  {
    switch (codes[index])
    {
      case 0:
        style = CursesStyle{};
        break;
      case 1:
        style.attributes |= A_BOLD;
        break;
      case 7:
        style.attributes |= A_REVERSE;
        break;
      case 38:
        if (index + 4 < codes.size() && codes[index + 1] == 2)
        {
          auto const red = codes[index + 2];
          auto const green = codes[index + 3];
          auto const blue = codes[index + 4];
          if (red > 220 && green > 180 && blue < 80)
          {
            style.color = ColorRole::Warning;
          }
          else if (red > 220 && green < 150 && blue < 150)
          {
            style.color = ColorRole::Error;
          }
          else if (red < 100 && green > 180 && blue > 120)
          {
            style.color = ColorRole::Success;
          }
          else if (blue > red && blue > green)
          {
            style.color = ColorRole::Accent;
          }
          else if (red < 180 && green < 180 && blue < 190)
          {
            style.color = ColorRole::Muted;
          }
          else
          {
            style.color = ColorRole::Text;
          }
          index += 4;
        }
        break;
      case 48:
        if (index + 4 < codes.size() && codes[index + 1] == 2)
        {
          auto const red = codes[index + 2];
          auto const green = codes[index + 3];
          auto const blue = codes[index + 4];
          style.background = (red > 15 || green > 20 || blue > 30) ? BackgroundRole::Composer : BackgroundRole::Screen;
          index += 4;
        }
        break;
      default:
        break;
    }
  }
}

void add_text_chunk(std::string_view text, CursesStyle style)
{
  if (text.empty())
    return;
  attrset(curses_attributes(style));
  static_cast<void>(addnstr(text.data(), static_cast<int>(std::min<std::size_t>(text.size(), INT_MAX))));
}

void draw_styled_line(std::string_view line)
{
  CursesStyle style{.attributes = A_NORMAL, .color = ColorRole::Text, .background = BackgroundRole::Screen};
  std::vector<int> codes;
  std::size_t chunk_start = 0;
  for (std::size_t index = 0; index < line.size();)
  {
    if (line[index] != '\x1b' || index + 1 >= line.size() || line[index + 1] != '[')
    {
      ++index;
      continue;
    }
    auto const end = line.find('m', index + 2);
    if (end == std::string_view::npos)
    {
      ++index;
      continue;
    }
    add_text_chunk(line.substr(chunk_start, index - chunk_start), style);
    if (parse_sgr_codes(line.substr(index + 2, end - index - 2), codes))
    {
      apply_sgr_codes(codes, style);
    }
    index = end + 1;
    chunk_start = index;
  }
  add_text_chunk(line.substr(chunk_start), style);
  attrset(curses_attributes(CursesStyle{.attributes = A_NORMAL, .color = ColorRole::Text, .background = BackgroundRole::Screen}));
  clrtoeol();
}

CursorPlacement input_cursor_placement(ComposerSnapshot const& snapshot, std::size_t rendered_line_count, std::size_t width)
{
  auto const input_lines = detail::input_render_lines(snapshot.input);
  auto const composer_lines = detail::composer_block_line_count(snapshot, rendered_line_count);
  auto const layout = detail::composer_input_layout(input_lines.size(), composer_lines, snapshot.draft_scroll_offset);
  auto const cursor_line = detail::input_cursor_line(snapshot);
  auto const visible_cursor_line = cursor_line < layout.first_visible ? std::size_t{0} : cursor_line - layout.first_visible;
  auto const composer_start_row = rendered_line_count >= composer_lines ? rendered_line_count - composer_lines : std::size_t{0};
  auto const visible_line = std::min(visible_cursor_line, layout.visible_input_lines == 0 ? std::size_t{0} : layout.visible_input_lines - 1);
  auto const column = detail::input_cursor_column(snapshot, width);
  return {.row = composer_start_row + layout.top_padding + visible_line, .column = column == 0 ? std::size_t{0} : column - 1};
}

constexpr auto kSidebarWidth = std::size_t{38};
constexpr auto kSidebarMinTerminalWidth = std::size_t{112};

bool sidebar_visible(ComposerSnapshot const& snapshot, std::size_t width)
{
  return snapshot.sidebar.has_value() && width >= kSidebarMinTerminalWidth;
}

std::size_t main_width_for(ComposerSnapshot const& snapshot, std::size_t width)
{
  if (!sidebar_visible(snapshot, width))
    return width;
  auto const sidebar_width = std::min<std::size_t>(kSidebarWidth, width / 3);
  return width > sidebar_width + 1 ? width - sidebar_width - 1 : width;
}

std::string status_marker(ToolTimelineStatus status)
{
  switch (status)
  {
    case ToolTimelineStatus::Running:
      return std::string(kSgrWarning) + "[~]" + std::string(kSgrReset);
    case ToolTimelineStatus::Success:
      return std::string(kSgrSuccess) + "[+]" + std::string(kSgrReset);
    case ToolTimelineStatus::Error:
      return std::string(kSgrError) + "[x]" + std::string(kSgrReset);
  }
  return std::string(kSgrDim) + "[?]" + std::string(kSgrReset);
}

void push_sidebar_line(std::vector<std::string>& lines, std::string line, std::size_t width)
{
  lines.push_back(detail::fit_line_preserving_sgr(" " + std::move(line), width));
}

std::optional<std::string> percent_text_from_token_status(std::optional<std::string> const& token_status)
{
  if (!token_status || token_status->empty())
    return std::nullopt;
  auto const open = token_status->rfind('(');
  if (open == std::string::npos)
    return std::nullopt;
  auto const percent = token_status->find('%', open + 1);
  if (percent == std::string::npos || percent <= open + 1)
    return std::nullopt;
  return token_status->substr(open + 1, percent - open - 1);
}

std::string context_pressure_line(std::string const& percent_text)
{
  double value = 0.0;
  double scale = 1.0;
  bool fractional = false;
  bool parsed_digit = false;
  for (auto const ch : percent_text)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (std::isdigit(byte) != 0)
    {
      parsed_digit = true;
      auto const digit = static_cast<double>(byte - static_cast<unsigned char>('0'));
      if (fractional)
      {
        scale *= 10.0;
        value += digit / scale;
      }
      else
      {
        value = (value * 10.0) + digit;
      }
    }
    else if (ch == '.' && !fractional)
    {
      fractional = true;
    }
    else
    {
      break;
    }
  }
  if (!parsed_digit)
    value = 0.0;
  auto const level = value >= 90.0   ? std::string("critical")
                     : value >= 70.0 ? std::string("high")
                     : value >= 40.0 ? std::string("moderate")
                                     : std::string("low");
  auto const color = value >= 70.0 ? kSgrWarning : kSgrDim;
  return std::string("context pressure ") + std::string(color) + level + " " + sanitize_terminal_text(percent_text) + "%" + std::string(kSgrReset);
}

std::vector<std::string> render_sidebar(SidebarSnapshot const& sidebar, std::size_t width, std::size_t height)
{
  std::vector<std::string> lines;
  lines.reserve(height);
  push_sidebar_line(lines, std::string(kSgrBold) + std::string(kSgrAccent) + "AVA" + std::string(kSgrReset), width);
  push_sidebar_line(lines, std::string(kSgrDim) + "live session" + std::string(kSgrReset), width);
  push_sidebar_line(lines, "", width);

  push_sidebar_line(lines, std::string(kSgrBold) + "Activity" + std::string(kSgrReset), width);
  auto const has_running_activity =
      std::ranges::any_of(sidebar.activity, [](SidebarActivityItem const& activity) { return activity.status == ToolTimelineStatus::Running; });
  if (!has_running_activity)
  {
    push_sidebar_line(lines, std::string(kSgrDim) + "idle" + std::string(kSgrReset), width);
  }
  else
  {
    for (auto const& activity : sidebar.activity)
    {
      if (activity.status != ToolTimelineStatus::Running)
        continue;
      auto line = status_marker(activity.status) + " " + sanitize_terminal_text(activity.label);
      if (!activity.detail.empty())
      {
        line += " " + std::string(kSgrDim) + sanitize_terminal_text(activity.detail) + std::string(kSgrReset);
      }
      push_sidebar_line(lines, std::move(line), width);
      if (lines.size() >= height)
        return lines;
    }
  }
  push_sidebar_line(lines, "", width);

  push_sidebar_line(lines, std::string(kSgrBold) + "Modified Files" + std::string(kSgrReset), width);
  if (sidebar.modified_files.empty())
  {
    push_sidebar_line(lines, std::string(kSgrDim) + "no file changes yet" + std::string(kSgrReset), width);
  }
  else
  {
    for (auto const& file : sidebar.modified_files)
    {
      auto line = sanitize_terminal_text(file.path);
      if (file.added || file.removed)
      {
        line += " ";
        if (file.added)
          line += std::string(kSgrSuccess) + "+" + std::to_string(*file.added) + std::string(kSgrReset);
        if (file.removed)
          line += " " + std::string(kSgrError) + "-" + std::to_string(*file.removed) + std::string(kSgrReset);
      }
      else
      {
        line += " " + std::string(kSgrDim) + "changed" + std::string(kSgrReset);
      }
      push_sidebar_line(lines, std::move(line), width);
      if (lines.size() >= height)
        return lines;
    }
  }
  push_sidebar_line(lines, "", width);

  push_sidebar_line(lines, std::string(kSgrBold) + "Session" + std::string(kSgrReset), width);
  push_sidebar_line(lines, "mode " + sanitize_terminal_text(sidebar.mode), width);
  push_sidebar_line(lines, "model " + sanitize_terminal_text(sidebar.provider) + "/" + sanitize_terminal_text(sidebar.model), width);
  push_sidebar_line(lines, "session " + sanitize_terminal_text(sidebar.session_id), width);
  if (!sidebar.session_path.empty())
    push_sidebar_line(lines, "path " + sanitize_terminal_text(sidebar.session_path), width);
  if (sidebar.session_entry_count.has_value())
  {
    push_sidebar_line(lines, "entries " + std::to_string(*sidebar.session_entry_count), width);
  }
  if (!sidebar.workspace.empty())
    push_sidebar_line(lines, "cwd " + sanitize_terminal_text(sidebar.workspace), width);
  if (!sidebar.git_branch.empty())
    push_sidebar_line(lines, "branch " + sanitize_terminal_text(sidebar.git_branch), width);
  if (sidebar.reasoning_status.has_value())
    push_sidebar_line(lines, "reasoning " + sanitize_terminal_text(*sidebar.reasoning_status), width);
  push_sidebar_line(lines, "usage " + sanitize_terminal_text(sidebar.token_status.value_or("tokens unknown")), width);
  if (auto const percent = percent_text_from_token_status(sidebar.token_status))
  {
    push_sidebar_line(lines, context_pressure_line(*percent), width);
  }
  if (sidebar.context_source_count.has_value())
  {
    push_sidebar_line(lines, "context sources " + std::to_string(*sidebar.context_source_count), width);
  }
  else
  {
    push_sidebar_line(lines, std::string("context sources ") + std::string(kSgrDim) + "unknown" + std::string(kSgrReset), width);
  }

  while (lines.size() + 1 < height) lines.emplace_back();
  if (!sidebar.version.empty() && lines.size() < height)
  {
    push_sidebar_line(lines, std::string(kSgrDim) + "AVA " + sidebar.version + std::string(kSgrReset), width);
  }
  while (lines.size() < height) lines.emplace_back();
  return lines;
}

std::vector<std::string> render_composer_main(ComposerSnapshot snapshot, std::size_t width, std::size_t height)
{
  snapshot.width = width;
  snapshot.height = height;
  snapshot.sidebar = std::nullopt;
  return render_composer(snapshot);
}

std::string pad_line_to_width(std::string line, std::size_t width)
{
  auto fitted = detail::fit_line_preserving_sgr(std::move(line), width);
  auto const columns = detail::terminal_text_columns(fitted);
  if (columns < width)
    fitted.append(width - columns, ' ');
  return fitted;
}

std::size_t modal_width_for(std::size_t width)
{
  if (width < 48)
    return width;
  return std::min<std::size_t>(76, std::max<std::size_t>(44, (width * 4) / 5));
}

std::size_t modal_height_for(std::size_t height)
{
  if (height < 10)
    return height;
  return std::min<std::size_t>(22, height > 4 ? height - 4 : height);
}

std::vector<std::string> overlay_question_modal(std::vector<std::string> lines, QuestionPromptView const& prompt, std::size_t width, std::size_t height)
{
  while (lines.size() < height) lines.emplace_back();
  auto const modal_width = std::min(modal_width_for(width), width);
  auto const modal_height = std::min(modal_height_for(height), height);
  auto const modal_lines = detail::render_question_modal(prompt, modal_width, modal_height);
  auto const top = height > modal_lines.size() ? (height - modal_lines.size()) / 2 : std::size_t{0};
  auto const left = width > modal_width ? (width - modal_width) / 2 : std::size_t{0};
  auto const right = width > left + modal_width ? width - left - modal_width : std::size_t{0};
  for (std::size_t index = 0; index < modal_lines.size() && top + index < lines.size(); ++index)
  {
    lines[top + index] = std::string(left, ' ') + modal_lines[index] + std::string(right, ' ');
  }
  return lines;
}

std::vector<std::string> overlay_select_list_modal(std::vector<std::string> lines, SelectListView const& view, std::size_t width, std::size_t height)
{
  while (lines.size() < height) lines.emplace_back();
  auto const modal_width = std::min(modal_width_for(width), width);
  auto const modal_height = std::min(modal_height_for(height), height);
  auto const modal_lines = detail::render_select_list_modal(view, modal_width, modal_height);
  auto const top = height > modal_lines.size() ? (height - modal_lines.size()) / 2 : std::size_t{0};
  auto const left = width > modal_width ? (width - modal_width) / 2 : std::size_t{0};
  auto const right = width > left + modal_width ? width - left - modal_width : std::size_t{0};
  for (std::size_t index = 0; index < modal_lines.size() && top + index < lines.size(); ++index)
  {
    lines[top + index] = std::string(left, ' ') + modal_lines[index] + std::string(right, ' ');
  }
  return lines;
}

std::vector<std::string> render_queued_message_lines(ComposerSnapshot const& snapshot, std::size_t width, std::size_t max_lines)
{
  std::vector<std::string> lines;
  if (max_lines == 0 || snapshot.queued_messages.empty())
    return lines;

  auto const visible_count = std::min(snapshot.queued_messages.size(), max_lines);
  lines.reserve(visible_count);
  auto const start = snapshot.queued_messages.size() - visible_count;
  for (std::size_t index = start; index < snapshot.queued_messages.size(); ++index)
  {
    auto const& item = snapshot.queued_messages[index];
    auto line = std::string(kSgrDim) + "queued " + sanitize_terminal_text(item.kind) + std::string(kSgrReset) + " " + sanitize_terminal_text(item.text);
    if (index == snapshot.queued_messages.size() - 1)
    {
      line += " " + std::string(kSgrDim) + "(/restore latest)" + std::string(kSgrReset);
    }
    lines.push_back(detail::screen_surface_line(std::move(line), width));
  }
  if (start > 0 && !lines.empty())
  {
    lines.front() = detail::screen_surface_line(std::string(kSgrDim) + "queued +" + std::to_string(start) + " more" + std::string(kSgrReset), width);
  }
  return lines;
}

}  // namespace

std::vector<std::string> render_composer(ComposerSnapshot const& snapshot)
{
  auto const width = std::max<std::size_t>(detail::kMinWidth, snapshot.width);
  auto const height = std::max<std::size_t>(detail::kMinHeight, snapshot.height);
  if (sidebar_visible(snapshot, width))
  {
    auto const sidebar_width = std::min<std::size_t>(kSidebarWidth, width / 3);
    auto const main_width = main_width_for(snapshot, width);
    auto main_lines = render_composer_main(snapshot, main_width, height);
    auto sidebar_lines = render_sidebar(*snapshot.sidebar, sidebar_width, height);
    std::vector<std::string> combined;
    combined.reserve(height);
    for (std::size_t row = 0; row < height; ++row)
    {
      auto const main_line = row < main_lines.size() ? main_lines[row] : std::string{};
      auto const sidebar_line = row < sidebar_lines.size() ? sidebar_lines[row] : std::string{};
      combined.push_back(pad_line_to_width(main_line, main_width) + std::string(kSgrDim) + "│" + std::string(kSgrReset) +
                         pad_line_to_width(sidebar_line, sidebar_width));
    }
    return combined;
  }
  if (snapshot.question_prompt && snapshot.question_prompt->modal)
  {
    auto base = snapshot;
    auto const prompt = *base.question_prompt;
    base.question_prompt = std::nullopt;
    return overlay_question_modal(render_composer(base), prompt, width, height);
  }
  if (snapshot.select_list)
  {
    auto base = snapshot;
    auto const view = *base.select_list;
    base.select_list = std::nullopt;
    return overlay_select_list_modal(render_composer(base), view, width, height);
  }
  std::vector<std::string> lines;
  lines.reserve(height);

  auto const prompt_active = snapshot.permission_prompt.has_value() || snapshot.question_prompt.has_value();
  auto const normal_composer_lines = detail::composer_block_line_count(snapshot, height);
  auto const fixed_lines = normal_composer_lines;
  auto const max_prompt_lines = height > fixed_lines ? height - fixed_lines : 0;
  auto const prompt_line_limit = snapshot.permission_prompt && !snapshot.permission_prompt->diff_preview.empty() ? std::size_t{12} : std::size_t{7};
  auto const prompt_line_budget = prompt_active ? std::min(prompt_line_limit, max_prompt_lines) : 0;
  auto permission_lines =
      snapshot.permission_prompt ? detail::render_permission_prompt(*snapshot.permission_prompt, width, prompt_line_budget) : std::vector<std::string>{};
  auto question_lines =
      snapshot.question_prompt ? detail::render_question_prompt(*snapshot.question_prompt, width, prompt_line_budget) : std::vector<std::string>{};
  auto const fixed_and_prompt_lines = fixed_lines + permission_lines.size() + question_lines.size();
  auto const palette_line_budget = (height > fixed_and_prompt_lines && !prompt_active && !snapshot.slash_palette_suppressed)
                                       ? std::min(detail::kMaxPaletteLines, height - fixed_and_prompt_lines)
                                       : 0;
  auto palette_lines = detail::render_slash_palette(snapshot, width, palette_line_budget);
  auto const fixed_prompt_palette_lines = fixed_and_prompt_lines + palette_lines.size();
  auto const queued_line_budget = (height > fixed_prompt_palette_lines && !prompt_active) ? std::min<std::size_t>(3, height - fixed_prompt_palette_lines) : 0;
  auto queued_lines = render_queued_message_lines(snapshot, width, queued_line_budget);

  auto const non_transcript_lines = fixed_lines + queued_lines.size() + palette_lines.size() + permission_lines.size() + question_lines.size();
  auto const transcript_height = height > non_transcript_lines ? height - non_transcript_lines : 0;
  auto const rendered_transcript = detail::render_transcript_lines(snapshot.transcript, width, snapshot.tool_details_visible, snapshot.thinking_visible);
  auto const visible_transcript = detail::visible_transcript_lines(rendered_transcript, width, transcript_height, snapshot.transcript_scroll_offset);

  lines.insert(lines.end(), visible_transcript.begin(), visible_transcript.end());
  while (lines.size() < transcript_height)
  {
    lines.push_back("");
  }

  for (auto const& line : palette_lines)
  {
    lines.push_back(line);
  }
  for (auto const& line : queued_lines)
  {
    lines.push_back(line);
  }
  for (auto const& line : permission_lines)
  {
    lines.push_back(line);
  }
  for (auto const& line : question_lines)
  {
    lines.push_back(line);
  }
  auto const composer_lines = detail::render_composer_block(snapshot, width, normal_composer_lines);
  lines.insert(lines.end(), composer_lines.begin(), composer_lines.end());
  return lines;
}

std::size_t composer_main_width(ComposerSnapshot const& snapshot)
{
  auto const width = std::max<std::size_t>(detail::kMinWidth, snapshot.width);
  return main_width_for(snapshot, width);
}

bool draw_screen(ComposerSnapshot const& snapshot)
{
  initialize_color_pairs();
  if (has_colors())
  {
    static_cast<void>(bkgd(curses_attributes(CursesStyle{.attributes = A_NORMAL, .color = ColorRole::Text, .background = BackgroundRole::Screen}) | ' '));
  }
  auto const width = std::max<std::size_t>(detail::kMinWidth, snapshot.width);
  auto const main_width = composer_main_width(snapshot);
  auto const lines = render_composer(snapshot);

  auto const cursor_visible = !snapshot.permission_prompt && !snapshot.question_prompt && !snapshot.select_list;
  static_cast<void>(curs_set(0));
  static_cast<void>(leaveok(stdscr, cursor_visible ? FALSE : TRUE));

  // Every visible row is repainted below. Avoid a full-screen blank pass because it makes
  // the sidebar flash during transcript scrolls on terminals with slower refreshes.
  for (std::size_t index = 0; index < lines.size(); ++index)
  {
    if (index > static_cast<std::size_t>(LINES > 0 ? LINES - 1 : 0))
      break;
    move(static_cast<int>(index), 0);
    draw_styled_line(detail::screen_surface_line(lines[index], width));
  }

  if (cursor_visible)
  {
    auto const cursor = input_cursor_placement(snapshot, lines.size(), main_width);
    move(static_cast<int>(std::min<std::size_t>(cursor.row, LINES > 0 ? LINES - 1 : 0)),
         static_cast<int>(std::min<std::size_t>(cursor.column, COLS > 0 ? COLS - 1 : 0)));
    static_cast<void>(curs_set(1));
  }

  return wnoutrefresh(stdscr) != ERR && doupdate() != ERR;
}

}  // namespace ava::tui
