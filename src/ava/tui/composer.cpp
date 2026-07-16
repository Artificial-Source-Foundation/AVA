#include "sys.h"
#include "ava/tui/composer.h"

#include "ava/tui/composer_internal.h"
#include "ava/tui/theme.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
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

enum class ColorRole {
  Text,
  Muted,
  Success,
  Warning,
  Error,
  Accent,
};

enum class BackgroundRole {
  Screen,
  Composer,
};

struct CursesStyle {
  attr_t attributes = A_NORMAL;
  ColorRole color = ColorRole::Text;
  BackgroundRole background = BackgroundRole::Screen;
};

struct CursorPlacement {
  std::size_t row = 0;
  std::size_t column = 0;
};

std::string strip_sgr_sequences(std::string_view text)
{
  std::string stripped;
  stripped.reserve(text.size());
  for (std::size_t index = 0; index < text.size();) {
    auto const before = index;
    if (detail::skip_sgr_sequence(text, index)) {
      continue;
    }
    stripped.push_back(text[before]);
    ++index;
  }
  return stripped;
}

std::vector<std::string> strip_sgr_frame(std::vector<std::string> lines)
{
  for (auto& line : lines) {
    line = strip_sgr_sequences(line);
  }
  return lines;
}

bool line_contains_osc_sequence(std::string_view line)
{
  for (std::size_t index = 0; index < line.size();) {
    if (detail::skip_osc_sequence(line, index)) return true;
    ++index;
  }
  return false;
}

void initialize_color_pairs()
{
  static std::optional<std::string> initialized_theme;
  auto const theme = active_tui_theme();
  auto const theme_key = theme.name + "|" + theme.badge + "|" + theme.revision;
  if (theme.kind == TuiThemeKind::Plain || !has_colors()) {
    initialized_theme = theme_key;
    return;
  }
  if (initialized_theme && *initialized_theme == theme_key) return;
  initialized_theme = theme_key;
  auto screen_bg = COLOR_BLACK;
  auto composer_bg = COLOR_BLACK;
  auto text_fg = COLOR_WHITE;
  auto muted_fg = COLOR_CYAN;
  auto success_fg = COLOR_GREEN;
  auto warning_fg = COLOR_YELLOW;
  auto error_fg = COLOR_RED;
  auto accent_fg = COLOR_CYAN;
  auto color_or_default = [](int value, short fallback) -> short {
    if (value < 0) return -1;
    if (value <= SHRT_MAX && value < COLORS) return static_cast<short>(value);
    return fallback;
  };
  if (theme.kind == TuiThemeKind::Custom && theme.palette) {
    screen_bg = color_or_default(theme.palette->screen_bg, COLOR_BLACK);
    composer_bg = color_or_default(theme.palette->composer_bg, screen_bg);
    text_fg = color_or_default(theme.palette->text, COLOR_WHITE);
    muted_fg = color_or_default(theme.palette->muted, COLOR_CYAN);
    success_fg = color_or_default(theme.palette->success, COLOR_GREEN);
    warning_fg = color_or_default(theme.palette->warning, COLOR_YELLOW);
    error_fg = color_or_default(theme.palette->error, COLOR_RED);
    accent_fg = color_or_default(theme.palette->accent, COLOR_CYAN);
  } else if (theme.kind == TuiThemeKind::Light) {
    screen_bg = COLOR_WHITE;
    composer_bg = COLOR_WHITE;
    text_fg = COLOR_BLACK;
    muted_fg = COLOR_BLUE;
    accent_fg = COLOR_BLUE;
    if (can_change_color() && COLORS > kColorComposerBg) {
      static_cast<void>(init_color(kColorScreenBg, 965, 971, 984));
      static_cast<void>(init_color(kColorComposerBg, 914, 933, 961));
      screen_bg = kColorScreenBg;
      composer_bg = kColorComposerBg;
    }
  } else if (can_change_color() && COLORS > kColorComposerBg) {
    static_cast<void>(init_color(kColorScreenBg, 43, 55, 78));
    static_cast<void>(init_color(kColorComposerBg, 102, 122, 180));
    screen_bg = kColorScreenBg;
    composer_bg = kColorComposerBg;
  }
  static_cast<void>(init_pair(kPairText, text_fg, screen_bg));
  static_cast<void>(init_pair(kPairMuted, muted_fg, screen_bg));
  static_cast<void>(init_pair(kPairSuccess, success_fg, screen_bg));
  static_cast<void>(init_pair(kPairWarning, warning_fg, screen_bg));
  static_cast<void>(init_pair(kPairError, error_fg, screen_bg));
  static_cast<void>(init_pair(kPairAccent, accent_fg, screen_bg));
  static_cast<void>(init_pair(kPairScreen, text_fg, screen_bg));
  static_cast<void>(init_pair(kPairComposer, text_fg, composer_bg));
  static_cast<void>(init_pair(kPairComposerText, text_fg, composer_bg));
  static_cast<void>(init_pair(kPairComposerMuted, muted_fg, composer_bg));
  static_cast<void>(init_pair(kPairComposerSuccess, success_fg, composer_bg));
  static_cast<void>(init_pair(kPairComposerWarning, warning_fg, composer_bg));
  static_cast<void>(init_pair(kPairComposerError, error_fg, composer_bg));
  static_cast<void>(init_pair(kPairComposerAccent, accent_fg, composer_bg));
}

short color_pair_for(CursesStyle const& style)
{
  if (style.background == BackgroundRole::Composer) {
    switch (style.color) {
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

  switch (style.color) {
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
  if (tui_plain_output()) return style.attributes;
  if (!has_colors()) return style.attributes;
  return style.attributes | COLOR_PAIR(color_pair_for(style));
}

bool parse_sgr_codes(std::string_view sequence, std::vector<int>& codes)
{
  codes.clear();
  if (sequence.empty()) return false;
  std::size_t start = 0;
  while (start <= sequence.size()) {
    auto const end = sequence.find(';', start);
    auto const token = sequence.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (token.empty()) {
      codes.push_back(0);
    } else {
      auto const token_text = std::string(token);
      char* parsed_end = nullptr;
      auto const value = std::strtol(token_text.c_str(), &parsed_end, 10);
      if (parsed_end == nullptr || *parsed_end != '\0') return false;
      codes.push_back(static_cast<int>(value));
    }
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return true;
}

void apply_sgr_codes(std::vector<int> const& codes, CursesStyle& style)
{
  if (codes.empty()) {
    style = CursesStyle{};
    return;
  }
  for (std::size_t index = 0; index < codes.size(); ++index) {
    switch (codes[index]) {
      case 0:
        style = CursesStyle{};
        break;
      case 1:
        style.attributes |= A_BOLD;
        break;
      case 3:
#ifdef A_ITALIC
        style.attributes |= A_ITALIC;
#endif
        break;
      case 4:
        style.attributes |= A_UNDERLINE;
        break;
      case 7:
        style.attributes |= A_REVERSE;
        break;
      case 9:
        // ncurses has no portable strike-through attribute. The snapshot renderer
        // still emits SGR 9 for ANSI-capable output; curses degrades to plain text.
        break;
      case 38:
        if (index + 4 < codes.size() && codes[index + 1] == 2) {
          auto const red = codes[index + 2];
          auto const green = codes[index + 3];
          auto const blue = codes[index + 4];
          if (red > 220 && green > 180 && blue < 80) {
            style.color = ColorRole::Warning;
          } else if (red > 220 && green < 150 && blue < 150) {
            style.color = ColorRole::Error;
          } else if (red < 100 && green > 180 && blue > 120) {
            style.color = ColorRole::Success;
          } else if (blue > red && blue > green) {
            style.color = ColorRole::Accent;
          } else if (red < 180 && green < 180 && blue < 190) {
            style.color = ColorRole::Muted;
          } else {
            style.color = ColorRole::Text;
          }
          index += 4;
        }
        break;
      case 48:
        if (index + 4 < codes.size() && codes[index + 1] == 2) {
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
  if (text.empty()) return;
  attrset(curses_attributes(style));
  static_cast<void>(addnstr(text.data(), static_cast<int>(std::min<std::size_t>(text.size(), INT_MAX))));
}

void draw_styled_line(std::string_view line)
{
  CursesStyle style{.attributes = A_NORMAL, .color = ColorRole::Text, .background = BackgroundRole::Screen};
  std::vector<int> codes;
  std::size_t chunk_start = 0;
  for (std::size_t index = 0; index < line.size();) {
    if (line[index] != '\x1b' || index + 1 >= line.size()) {
      ++index;
      continue;
    }
    if (line[index + 1] == ']') {
      auto const osc_start = index;
      if (!detail::skip_osc_sequence(line, index)) {
        index = osc_start + 1;
        continue;
      }
      add_text_chunk(line.substr(chunk_start, osc_start - chunk_start), style);
      chunk_start = index;
      continue;
    }
    if (line[index + 1] != '[') {
      ++index;
      continue;
    }
    auto const end = line.find('m', index + 2);
    if (end == std::string_view::npos) {
      ++index;
      continue;
    }
    add_text_chunk(line.substr(chunk_start, index - chunk_start), style);
    if (parse_sgr_codes(line.substr(index + 2, end - index - 2), codes)) {
      apply_sgr_codes(codes, style);
    }
    index = end + 1;
    chunk_start = index;
  }
  add_text_chunk(line.substr(chunk_start), style);
  attrset(curses_attributes(
      CursesStyle{.attributes = A_NORMAL, .color = ColorRole::Text, .background = BackgroundRole::Screen}));
  clrtoeol();
}

CursorPlacement input_cursor_placement(ComposerSnapshot const& snapshot, std::size_t rendered_line_count,
                                       std::size_t width)
{
  auto const input_lines = detail::input_render_line_spans(snapshot.input, width);
  auto const composer_lines = detail::composer_block_line_count(snapshot, rendered_line_count, width);
  auto const layout = detail::composer_input_layout(input_lines.size(), composer_lines, snapshot.draft_scroll_offset);
  auto const cursor_line = detail::input_cursor_line(snapshot, width);
  auto const visible_cursor_line =
      cursor_line < layout.first_visible ? std::size_t{0} : cursor_line - layout.first_visible;
  auto const composer_start_row =
      rendered_line_count >= composer_lines ? rendered_line_count - composer_lines : std::size_t{0};
  auto const visible_line =
      std::min(visible_cursor_line, layout.visible_input_lines == 0 ? std::size_t{0} : layout.visible_input_lines - 1);
  auto const column = detail::input_cursor_column(snapshot, width);
  return {.row = composer_start_row + layout.top_padding + visible_line,
          .column = column == 0 ? std::size_t{0} : column - 1};
}

constexpr auto kSidebarWidth = std::size_t{38};
constexpr auto kSidebarMinTerminalWidth = std::size_t{112};

bool sidebar_visible(ComposerSnapshot const& snapshot, std::size_t width)
{
  return snapshot.sidebar.has_value() && width >= kSidebarMinTerminalWidth;
}

std::size_t main_width_for(ComposerSnapshot const& snapshot, std::size_t width)
{
  if (!sidebar_visible(snapshot, width)) return width;
  auto const sidebar_width = std::min<std::size_t>(kSidebarWidth, width / 3);
  return width > sidebar_width + 1 ? width - sidebar_width - 1 : width;
}

std::string status_marker(ToolTimelineStatus status)
{
  switch (status) {
    case ToolTimelineStatus::Running:
      return std::string(kSgrWarning) + "[~]" + std::string(kSgrReset);
    case ToolTimelineStatus::Success:
      return std::string(kSgrSuccess) + "[+]" + std::string(kSgrReset);
    case ToolTimelineStatus::Canceled:
      return std::string(kSgrDim) + "[-]" + std::string(kSgrReset);
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
  if (!token_status || token_status->empty()) return std::nullopt;
  auto const open = token_status->rfind('(');
  if (open == std::string::npos) return std::nullopt;
  auto const percent = token_status->find('%', open + 1);
  if (percent == std::string::npos || percent <= open + 1) return std::nullopt;
  return token_status->substr(open + 1, percent - open - 1);
}

std::string context_pressure_line(std::string const& percent_text)
{
  double value = 0.0;
  double scale = 1.0;
  bool fractional = false;
  bool parsed_digit = false;
  for (auto const ch : percent_text) {
    auto const byte = static_cast<unsigned char>(ch);
    if (std::isdigit(byte) != 0) {
      parsed_digit = true;
      auto const digit = static_cast<double>(byte - static_cast<unsigned char>('0'));
      if (fractional) {
        scale *= 10.0;
        value += digit / scale;
      } else {
        value = (value * 10.0) + digit;
      }
    } else if (ch == '.' && !fractional) {
      fractional = true;
    } else {
      break;
    }
  }
  if (!parsed_digit) value = 0.0;
  auto const level = value >= 90.0   ? std::string("critical")
                     : value >= 70.0 ? std::string("high")
                     : value >= 40.0 ? std::string("moderate")
                                     : std::string("low");
  auto const color = value >= 70.0 ? kSgrWarning : kSgrDim;
  return std::string("context pressure ") + std::string(color) + level + " " + sanitize_terminal_text(percent_text) +
         "%" + std::string(kSgrReset);
}

std::vector<std::string> render_sidebar(SidebarSnapshot const& sidebar, std::size_t width, std::size_t height)
{
  std::vector<std::string> lines;
  lines.reserve(height);
  push_sidebar_line(lines, std::string(kSgrBold) + std::string(kSgrAccent) + "AVA" + std::string(kSgrReset), width);
  push_sidebar_line(lines, std::string(kSgrDim) + "live session" + std::string(kSgrReset), width);
  push_sidebar_line(lines, "", width);

  push_sidebar_line(lines, std::string(kSgrBold) + "Activity" + std::string(kSgrReset), width);
  auto const has_running_activity = std::ranges::any_of(sidebar.activity, [](SidebarActivityItem const& activity) {
    return activity.status == ToolTimelineStatus::Running;
  });
  if (!has_running_activity) {
    push_sidebar_line(lines, std::string(kSgrDim) + "idle" + std::string(kSgrReset), width);
  } else {
    for (auto const& activity : sidebar.activity) {
      if (activity.status != ToolTimelineStatus::Running) continue;
      auto line = status_marker(activity.status) + " " + sanitize_terminal_text(activity.label);
      if (!activity.detail.empty()) {
        line += " " + std::string(kSgrDim) + sanitize_terminal_text(activity.detail) + std::string(kSgrReset);
      }
      push_sidebar_line(lines, std::move(line), width);
      if (lines.size() >= height) return lines;
    }
  }
  push_sidebar_line(lines, "", width);

  push_sidebar_line(lines, std::string(kSgrBold) + "Modified Files" + std::string(kSgrReset), width);
  if (sidebar.modified_files.empty()) {
    push_sidebar_line(lines, std::string(kSgrDim) + "no file changes yet" + std::string(kSgrReset), width);
  } else {
    for (auto const& file : sidebar.modified_files) {
      auto line = sanitize_terminal_text(file.path);
      if (file.added || file.removed) {
        line += " ";
        if (file.added) line += std::string(kSgrSuccess) + "+" + std::to_string(*file.added) + std::string(kSgrReset);
        if (file.removed)
          line += " " + std::string(kSgrError) + "-" + std::to_string(*file.removed) + std::string(kSgrReset);
      } else {
        line += " " + std::string(kSgrDim) + "changed" + std::string(kSgrReset);
      }
      push_sidebar_line(lines, std::move(line), width);
      if (lines.size() >= height) return lines;
    }
  }
  push_sidebar_line(lines, "", width);

  push_sidebar_line(lines, std::string(kSgrBold) + "Session" + std::string(kSgrReset), width);
  push_sidebar_line(lines, "mode " + sanitize_terminal_text(sidebar.mode), width);
  push_sidebar_line(
      lines, "model " + sanitize_terminal_text(sidebar.provider) + "/" + sanitize_terminal_text(sidebar.model), width);
  push_sidebar_line(lines, "session " + sanitize_terminal_text(sidebar.session_id), width);
  if (!sidebar.session_path.empty())
    push_sidebar_line(lines, "path " + sanitize_terminal_text(sidebar.session_path), width);
  if (sidebar.session_entry_count.has_value()) {
    push_sidebar_line(lines, "entries " + std::to_string(*sidebar.session_entry_count), width);
  }
  if (!sidebar.workspace.empty()) push_sidebar_line(lines, "cwd " + sanitize_terminal_text(sidebar.workspace), width);
  if (!sidebar.git_branch.empty())
    push_sidebar_line(lines, "branch " + sanitize_terminal_text(sidebar.git_branch), width);
  if (sidebar.reasoning_status.has_value())
    push_sidebar_line(lines, "reasoning " + sanitize_terminal_text(*sidebar.reasoning_status), width);
  push_sidebar_line(lines, "usage " + sanitize_terminal_text(sidebar.token_status.value_or("tokens unknown")), width);
  if (auto const percent = percent_text_from_token_status(sidebar.token_status)) {
    push_sidebar_line(lines, context_pressure_line(*percent), width);
  }
  if (sidebar.context_source_count.has_value()) {
    push_sidebar_line(lines, "context sources " + std::to_string(*sidebar.context_source_count), width);
  } else {
    push_sidebar_line(
        lines, std::string("context sources ") + std::string(kSgrDim) + "unknown" + std::string(kSgrReset), width);
  }

  while (lines.size() + 1 < height) lines.emplace_back();
  if (!sidebar.version.empty() && lines.size() < height) {
    push_sidebar_line(lines, std::string(kSgrDim) + "AVA " + sidebar.version + std::string(kSgrReset), width);
  }
  while (lines.size() < height) lines.emplace_back();
  return lines;
}

ComposerFrame render_composer_main_frame(ComposerSnapshot snapshot, std::size_t width, std::size_t height)
{
  snapshot.width = width;
  snapshot.height = height;
  snapshot.sidebar = std::nullopt;
  return render_composer_frame(snapshot);
}

std::string pad_line_to_width(std::string line, std::size_t width)
{
  auto fitted = detail::fit_line_preserving_sgr(std::move(line), width);
  auto const columns = detail::terminal_text_columns(fitted);
  if (columns < width) fitted.append(width - columns, ' ');
  return fitted;
}

std::size_t modal_width_for(std::size_t width)
{
  if (width < 48) return width;
  return std::min<std::size_t>(76, std::max<std::size_t>(44, (width * 4) / 5));
}

std::size_t modal_height_for(std::size_t height)
{
  if (height < 10) return height;
  return std::min<std::size_t>(22, height > 4 ? height - 4 : height);
}

std::vector<std::string> overlay_question_modal(std::vector<std::string> lines, QuestionPromptView const& prompt,
                                                std::size_t width, std::size_t height)
{
  while (lines.size() < height) lines.emplace_back();
  auto const modal_width = std::min(modal_width_for(width), width);
  auto const modal_height = std::min(modal_height_for(height), height);
  auto const modal_lines = detail::render_question_modal(prompt, modal_width, modal_height);
  auto const top = height > modal_lines.size() ? (height - modal_lines.size()) / 2 : std::size_t{0};
  auto const left = width > modal_width ? (width - modal_width) / 2 : std::size_t{0};
  auto const right = width > left + modal_width ? width - left - modal_width : std::size_t{0};
  for (std::size_t index = 0; index < modal_lines.size() && top + index < lines.size(); ++index) {
    lines[top + index] = std::string(left, ' ') + modal_lines[index] + std::string(right, ' ');
  }
  return lines;
}

std::vector<std::string> overlay_select_list_modal(std::vector<std::string> lines, SelectListView const& view,
                                                   std::size_t width, std::size_t height)
{
  while (lines.size() < height) lines.emplace_back();
  auto const modal_width = std::min(modal_width_for(width), width);
  auto const modal_height = std::min(modal_height_for(height), height);
  auto const modal_lines = detail::render_select_list_modal(view, modal_width, modal_height);
  auto const top = height > modal_lines.size() ? (height - modal_lines.size()) / 2 : std::size_t{0};
  auto const left = width > modal_width ? (width - modal_width) / 2 : std::size_t{0};
  auto const right = width > left + modal_width ? width - left - modal_width : std::size_t{0};
  for (std::size_t index = 0; index < modal_lines.size() && top + index < lines.size(); ++index) {
    lines[top + index] = std::string(left, ' ') + modal_lines[index] + std::string(right, ' ');
  }
  return lines;
}

std::vector<std::string> render_queued_message_lines(ComposerSnapshot const& snapshot, std::size_t width,
                                                      std::size_t max_lines)
{
  std::vector<std::string> lines;
  if (max_lines == 0 || snapshot.queued_messages.empty()) return lines;

  auto const visible_count = std::min(snapshot.queued_messages.size(), max_lines);
  lines.reserve(visible_count);
  auto const start = snapshot.queued_messages.size() - visible_count;
  for (std::size_t index = start; index < snapshot.queued_messages.size(); ++index) {
    auto const& item = snapshot.queued_messages[index];
    auto line = std::string(kSgrDim) + "queued " + sanitize_terminal_text(item.kind) + std::string(kSgrReset) + " " +
                sanitize_terminal_text(item.text);
    if (index == snapshot.queued_messages.size() - 1) {
      line += " " + std::string(kSgrDim) + "(/restore or Alt+Up latest)" + std::string(kSgrReset);
    }
    lines.push_back(detail::screen_surface_line(std::move(line), width));
  }
  if (start > 0 && !lines.empty()) {
    lines.front() = detail::screen_surface_line(
        std::string(kSgrDim) + "queued +" + std::to_string(start) + " more" + std::string(kSgrReset), width);
  }
  return lines;
}

struct PendingAttachmentRender {
  std::vector<std::string> lines;
  std::vector<TerminalGraphicOverlay> graphics;
};

std::optional<TerminalGraphicOverlay> pending_attachment_graphic(PendingAttachmentItem const& item, std::size_t width,
                                                                 std::size_t max_rows)
{
  if (!item.preview || item.preview->protocol == TerminalImageProtocol::None || !item.preview->base64_data ||
      item.preview->base64_data->empty() || max_rows == 0) {
    return std::nullopt;
  }
  auto const max_width = width > 4 ? std::min<std::size_t>(60, width - 4) : std::size_t{1};
  // TODO: Pass measured terminal cell pixel dimensions here once the TUI runtime has a safe
  // terminal-query seam. Until then image previews use calculate_image_cell_size's deterministic
  // fallback cell dimensions, so sizing remains bounded but not terminal-specific.
  auto const cells = calculate_image_cell_size(item.preview->dimensions, max_width, max_rows);
  if (cells.rows > max_rows) return std::nullopt;

  std::string sequence;
  switch (item.preview->protocol) {
    case TerminalImageProtocol::Kitty: {
      KittyImageOptions options;
      options.columns = cells.columns;
      options.rows = cells.rows;
      options.image_id = item.preview->image_id;
      options.move_cursor = false;
      sequence = encode_kitty_image(*item.preview->base64_data, options);
      break;
    }
    case TerminalImageProtocol::Iterm2: {
      Iterm2ImageOptions options;
      options.width = std::to_string(cells.columns);
      options.height = std::to_string(cells.rows);
      options.name = item.label;
      sequence = encode_iterm2_image(*item.preview->base64_data, options);
      break;
    }
    case TerminalImageProtocol::None:
      return std::nullopt;
  }
  return TerminalGraphicOverlay{.protocol = item.preview->protocol,
                                .row = 0,
                                .column = width > 4 ? std::size_t{2} : std::size_t{0},
                                .rows = cells.rows,
                                .columns = cells.columns,
                                .image_id = item.preview->protocol == TerminalImageProtocol::Kitty ? item.preview->image_id : std::nullopt,
                                .sequence = std::move(sequence)};
}

PendingAttachmentRender render_pending_attachment_lines(ComposerSnapshot const& snapshot, std::size_t width,
                                                        std::size_t max_lines)
{
  PendingAttachmentRender render;
  if (max_lines == 0 || snapshot.pending_attachments.empty()) return render;

  auto const visible_count = std::min(snapshot.pending_attachments.size(), max_lines);
  render.lines.reserve(max_lines);
  auto const start = snapshot.pending_attachments.size() - visible_count;
  for (std::size_t index = start; index < snapshot.pending_attachments.size(); ++index) {
    if (render.lines.size() >= max_lines) break;
    auto const& item = snapshot.pending_attachments[index];
    auto line = std::string(kSgrDim) + "attached image" + std::string(kSgrReset) + " " +
                sanitize_terminal_text(item.label);
    if (!item.detail.empty()) {
      line += " " + std::string(kSgrDim) + item.detail + std::string(kSgrReset);
    }
    if (index == snapshot.pending_attachments.size() - 1) {
      line += " " + std::string(kSgrDim) + "(next prompt)" + std::string(kSgrReset);
    }
    render.lines.push_back(detail::screen_surface_line(std::move(line), width));
    if (index == snapshot.pending_attachments.size() - 1 && render.lines.size() < max_lines) {
      auto graphic = pending_attachment_graphic(item, width, max_lines - render.lines.size());
      if (graphic) {
        graphic->row = render.lines.size();
        for (std::size_t row = 0; row < graphic->rows; ++row) {
          render.lines.push_back(detail::screen_surface_line("", width));
        }
        render.graphics.push_back(std::move(*graphic));
      }
    }
  }
  if (start > 0 && !render.lines.empty()) {
    render.lines.front() = detail::screen_surface_line(
        std::string(kSgrDim) + "attached +" + std::to_string(start) + " more images" + std::string(kSgrReset), width);
  }
  return render;
}

bool status_is_alert(std::string_view status)
{
  constexpr std::array kErrorPrefixes = {"invalid_argument:", "io:",       "not_found:",
                                         "permission_denied:", "provider:", "session:",
                                         "tool:",              "unknown:"};
  return std::ranges::any_of(kErrorPrefixes, [status](std::string_view prefix) { return status.starts_with(prefix); });
}

std::vector<std::string> render_status_alert_lines(std::string_view status, std::size_t width, std::size_t max_lines)
{
  std::vector<std::string> lines;
  if (max_lines == 0 || status.empty() || !status_is_alert(status)) return lines;

  auto const parts = split_lines(status);
  auto const visible_count = std::min(parts.size(), max_lines);
  lines.reserve(visible_count);
  for (std::size_t index = 0; index < visible_count; ++index) {
    auto line = std::string(index == 0 ? "! " : "  ") + sanitize_terminal_text(parts[index]);
    if (index + 1 == visible_count && parts.size() > visible_count) line += " ...";
    if (index == 0) {
      line = std::string(kSgrError) + line + std::string(kSgrReset);
    } else {
      line = std::string(kSgrDim) + line + std::string(kSgrReset);
    }
    lines.push_back(detail::fit_line_preserving_sgr(std::move(line), width));
  }
  return lines;
}

std::string render_transcript_scroll_indicator(ComposerSnapshot const& snapshot, std::size_t width)
{
  auto line = std::string("  ") + std::string(kSgrWarning) + "↓" + std::string(kSgrReset) + " " +
              std::string(kSgrDim) + "scrollback detached";
  if (snapshot.transcript_new_output_count > 0) {
    line += " · ";
    if (snapshot.transcript_new_output_count == 1) {
      line += "new output below";
    } else {
      line += "+" + std::to_string(snapshot.transcript_new_output_count) + " updates below";
    }
  }
  line += " · jump_to_bottom" + std::string(kSgrReset);
  return detail::fit_line_preserving_sgr(std::move(line), width);
}

std::size_t input_line_cursor_offset_for_columns(std::string_view line, std::size_t target_columns)
{
  if (target_columns == 0)
    return 0;
  std::size_t columns = 0;
  for (std::size_t index = 0; index < line.size();) {
    auto const byte = static_cast<unsigned char>(line[index]);
    if (byte == '\t') {
      columns += 2;
      ++index;
      if (columns >= target_columns)
        return index;
      continue;
    }
    if (byte < 0x20 || byte == 0x7F) {
      ++columns;
      ++index;
      if (columns >= target_columns)
        return index;
      continue;
    }

    auto const cell = detail::terminal_text_cell(line, index);
    if (cell.valid) {
      columns += cell.columns;
      index += cell.bytes;
    } else {
      ++columns;
      ++index;
    }
    if (columns >= target_columns)
      return index;
  }
  return line.size();
}

}  // namespace

ComposerFrame finish_frame(ComposerFrame frame)
{
  if (tui_plain_output()) {
    frame.lines = strip_sgr_frame(std::move(frame.lines));
    frame.graphics.clear();
  }
  return frame;
}

ComposerFrame render_composer_frame(ComposerSnapshot const& snapshot)
{
  auto const width = std::max<std::size_t>(detail::kMinWidth, snapshot.width);
  auto const height = std::max<std::size_t>(detail::kMinHeight, snapshot.height);
  if (sidebar_visible(snapshot, width)) {
    auto const sidebar_width = std::min<std::size_t>(kSidebarWidth, width / 3);
    auto const main_width = main_width_for(snapshot, width);
    auto main_frame = render_composer_main_frame(snapshot, main_width, height);
    auto sidebar_lines = render_sidebar(*snapshot.sidebar, sidebar_width, height);
    ComposerFrame combined;
    combined.lines.reserve(height);
    for (std::size_t row = 0; row < height; ++row) {
      auto const main_line = row < main_frame.lines.size() ? main_frame.lines[row] : std::string{};
      auto const sidebar_line = row < sidebar_lines.size() ? sidebar_lines[row] : std::string{};
      combined.lines.push_back(pad_line_to_width(main_line, main_width) + std::string(kSgrDim) + "│" +
                               std::string(kSgrReset) + pad_line_to_width(sidebar_line, sidebar_width));
    }
    combined.graphics = std::move(main_frame.graphics);
    return finish_frame(std::move(combined));
  }
  if (snapshot.question_prompt && snapshot.question_prompt->modal) {
    auto base = snapshot;
    auto const prompt = *base.question_prompt;
    base.question_prompt = std::nullopt;
    auto frame = render_composer_frame(base);
    frame.lines = overlay_question_modal(std::move(frame.lines), prompt, width, height);
    frame.graphics.clear();
    return finish_frame(std::move(frame));
  }
  if (snapshot.select_list) {
    auto base = snapshot;
    auto const view = *base.select_list;
    base.select_list = std::nullopt;
    auto frame = render_composer_frame(base);
    frame.lines = overlay_select_list_modal(std::move(frame.lines), view, width, height);
    frame.graphics.clear();
    return finish_frame(std::move(frame));
  }
  ComposerFrame frame;
  frame.lines.reserve(height);

  auto const prompt_active = snapshot.permission_prompt.has_value() || snapshot.question_prompt.has_value();
  auto const normal_composer_lines = detail::composer_block_line_count(snapshot, height, width);
  auto const fixed_lines = normal_composer_lines;
  auto const max_prompt_lines = height > fixed_lines ? height - fixed_lines : 0;
  auto const prompt_line_limit = snapshot.permission_prompt && !snapshot.permission_prompt->diff_preview.empty()
                                     ? std::size_t{12}
                                     : std::size_t{7};
  auto const prompt_line_budget = prompt_active ? std::min(prompt_line_limit, max_prompt_lines) : 0;
  auto permission_lines = snapshot.permission_prompt
                              ? detail::render_permission_prompt(*snapshot.permission_prompt, width, prompt_line_budget)
                              : std::vector<std::string>{};
  auto question_lines = snapshot.question_prompt
                            ? detail::render_question_prompt(*snapshot.question_prompt, width, prompt_line_budget)
                            : std::vector<std::string>{};
  auto const fixed_and_prompt_lines = fixed_lines + permission_lines.size() + question_lines.size();
  auto const palette_line_budget =
      (height > fixed_and_prompt_lines && !prompt_active && !snapshot.slash_palette_suppressed)
          ? std::min(detail::kMaxPaletteLines, height - fixed_and_prompt_lines)
          : 0;
  auto palette_lines = detail::render_slash_palette(snapshot, width, palette_line_budget);
  if (palette_lines.empty()) {
    palette_lines = detail::render_file_reference_palette(snapshot, width, palette_line_budget);
  }
  if (palette_lines.empty()) {
    palette_lines = detail::render_path_completion_palette(snapshot, width, palette_line_budget);
  }
  auto const fixed_prompt_palette_lines = fixed_and_prompt_lines + palette_lines.size();
  auto const queued_line_budget = (height > fixed_prompt_palette_lines && !prompt_active)
                                      ? std::min<std::size_t>(3, height - fixed_prompt_palette_lines)
                                      : 0;
  auto queued_lines = render_queued_message_lines(snapshot, width, queued_line_budget);
  auto const fixed_prompt_palette_queued_lines = fixed_prompt_palette_lines + queued_lines.size();
  auto const pending_attachment_line_budget = (height > fixed_prompt_palette_queued_lines && !prompt_active)
                                                  ? std::min<std::size_t>(8, height - fixed_prompt_palette_queued_lines)
                                                  : 0;
  auto pending_attachment_lines = render_pending_attachment_lines(snapshot, width, pending_attachment_line_budget);
  auto const fixed_prompt_palette_queued_attachment_lines =
      fixed_prompt_palette_queued_lines + pending_attachment_lines.lines.size();
  auto const status_alert_line_budget = (height > fixed_prompt_palette_queued_attachment_lines && !prompt_active)
                                            ? std::min<std::size_t>(3, height - fixed_prompt_palette_queued_attachment_lines)
                                            : 0;
  auto status_alert_lines = render_status_alert_lines(snapshot.status, width, status_alert_line_budget);

  auto const non_transcript_lines = fixed_lines + queued_lines.size() + pending_attachment_lines.lines.size() + status_alert_lines.size() +
                                    palette_lines.size() + permission_lines.size() + question_lines.size();
  auto const transcript_height = height > non_transcript_lines ? height - non_transcript_lines : 0;
  auto const transcript_tail_budget =
      snapshot.transcript_scroll_offset > std::numeric_limits<std::size_t>::max() - transcript_height
          ? std::numeric_limits<std::size_t>::max()
          : transcript_height + snapshot.transcript_scroll_offset;
  auto const rendered_transcript = detail::render_transcript_tail_lines(
      snapshot.transcript, width, transcript_tail_budget, snapshot.tool_details_visible, snapshot.thinking_visible);
  auto visible_transcript = detail::visible_transcript_lines(rendered_transcript, width, transcript_height,
                                                             snapshot.transcript_scroll_offset);
  if (snapshot.transcript_scroll_offset > 0 && !visible_transcript.empty()) {
    visible_transcript.front() = render_transcript_scroll_indicator(snapshot, width);
  }

  frame.lines.insert(frame.lines.end(), visible_transcript.begin(), visible_transcript.end());
  while (frame.lines.size() < transcript_height) {
    frame.lines.push_back("");
  }

  for (auto const& line : palette_lines) {
    frame.lines.push_back(line);
  }
  for (auto const& line : queued_lines) {
    frame.lines.push_back(line);
  }
  auto const pending_attachment_start_row = frame.lines.size();
  for (auto const& line : pending_attachment_lines.lines) {
    frame.lines.push_back(line);
  }
  for (auto graphic : pending_attachment_lines.graphics) {
    graphic.row += pending_attachment_start_row;
    frame.graphics.push_back(std::move(graphic));
  }
  for (auto const& line : status_alert_lines) {
    frame.lines.push_back(line);
  }
  for (auto const& line : permission_lines) {
    frame.lines.push_back(line);
  }
  for (auto const& line : question_lines) {
    frame.lines.push_back(line);
  }
  auto const composer_lines = detail::render_composer_block(snapshot, width, normal_composer_lines);
  frame.lines.insert(frame.lines.end(), composer_lines.begin(), composer_lines.end());
  return finish_frame(std::move(frame));
}

std::vector<std::string> render_composer(ComposerSnapshot const& snapshot)
{
  return render_composer_frame(snapshot).lines;
}

std::size_t composer_main_width(ComposerSnapshot const& snapshot)
{
  auto const width = std::max<std::size_t>(detail::kMinWidth, snapshot.width);
  return main_width_for(snapshot, width);
}

std::optional<std::size_t> composer_input_cursor_for_screen_position(ComposerSnapshot const& snapshot,
                                                                     std::size_t row,
                                                                     std::size_t column)
{
  if (row == 0 || column == 0 || snapshot.permission_prompt || snapshot.question_prompt || snapshot.select_list)
    return std::nullopt;

  auto const height = std::max<std::size_t>(detail::kMinHeight, snapshot.height);
  auto const width = composer_main_width(snapshot);
  if (column > width)
    return std::nullopt;

  auto const input_lines = detail::input_render_line_spans(snapshot.input, width);
  auto const composer_lines = detail::composer_block_line_count(snapshot, height, width);
  auto const layout = detail::composer_input_layout(input_lines.size(), composer_lines, snapshot.draft_scroll_offset);
  auto const composer_start_row = height >= composer_lines ? height - composer_lines : std::size_t{0};
  auto const row_index = row - 1;
  auto const first_input_row = composer_start_row + layout.top_padding;
  if (row_index < first_input_row)
    return std::nullopt;
  auto const visible_line = row_index - first_input_row;
  if (visible_line >= layout.visible_input_lines)
    return std::nullopt;
  auto const logical_line = layout.first_visible + visible_line;
  if (logical_line >= input_lines.size())
    return std::nullopt;

  auto const& input_line = input_lines[logical_line];
  auto const prefix_columns = detail::composer_input_prefix_columns(input_line.first_line);
  auto const target_columns = column <= prefix_columns ? std::size_t{0} : column - prefix_columns - 1;
  auto const line_offset = input_line_cursor_offset_for_columns(input_line.text, target_columns);
  return std::min(input_line.start + line_offset, input_line.end);
}

std::optional<std::size_t> select_list_selection_for_screen_position(ComposerSnapshot const& snapshot,
                                                                     std::size_t row,
                                                                     std::size_t column)
{
  if (row == 0 || column == 0 || !snapshot.select_list || snapshot.permission_prompt || snapshot.question_prompt)
    return std::nullopt;

  auto const width = composer_main_width(snapshot);
  auto const height = std::max<std::size_t>(detail::kMinHeight, snapshot.height);
  auto const modal_width = std::min(modal_width_for(width), width);
  auto const modal_height = std::min(modal_height_for(height), height);
  auto const top = height > modal_height ? (height - modal_height) / 2 : std::size_t{0};
  auto const left = width > modal_width ? (width - modal_width) / 2 : std::size_t{0};
  if (row <= top || row > top + modal_height)
    return std::nullopt;
  if (column <= left || column > left + modal_width)
    return std::nullopt;

  return detail::select_list_item_for_modal_row(*snapshot.select_list, row - top - 1, modal_width, modal_height);
}

bool draw_screen(ComposerSnapshot const& snapshot)
{
  static std::vector<std::size_t> active_kitty_image_ids;
  initialize_color_pairs();
  if (!tui_plain_output() && has_colors()) {
    static_cast<void>(
        bkgd(curses_attributes(
                 CursesStyle{.attributes = A_NORMAL, .color = ColorRole::Text, .background = BackgroundRole::Screen}) |
             ' '));
  } else {
    static_cast<void>(bkgd(A_NORMAL | ' '));
  }
  auto const width = std::max<std::size_t>(detail::kMinWidth, snapshot.width);
  auto const main_width = composer_main_width(snapshot);
  auto const frame = render_composer_frame(snapshot);
  auto const& lines = frame.lines;

  auto const cursor_visible = !snapshot.permission_prompt && !snapshot.question_prompt && !snapshot.select_list;
  static_cast<void>(curs_set(0));
  static_cast<void>(leaveok(stdscr, cursor_visible ? FALSE : TRUE));

  // Every visible row is repainted below. Avoid a full-screen blank pass because it makes
  // the sidebar flash during transcript scrolls on terminals with slower refreshes.
  std::vector<std::pair<std::size_t, std::string>> osc_overlay_lines;
  for (std::size_t index = 0; index < lines.size(); ++index) {
    if (index > static_cast<std::size_t>(LINES > 0 ? LINES - 1 : 0)) break;
    move(static_cast<int>(index), 0);
    auto surface_line = detail::screen_surface_line(lines[index], width);
    if (line_contains_osc_sequence(surface_line)) {
      osc_overlay_lines.emplace_back(index, surface_line);
    }
    draw_styled_line(surface_line);
  }

  auto const cursor = input_cursor_placement(snapshot, lines.size(), main_width);
  if (cursor_visible) {
    move(static_cast<int>(std::min<std::size_t>(cursor.row, LINES > 0 ? LINES - 1 : 0)),
         static_cast<int>(std::min<std::size_t>(cursor.column, COLS > 0 ? COLS - 1 : 0)));
    static_cast<void>(curs_set(1));
  }

  if (wnoutrefresh(stdscr) == ERR || doupdate() == ERR) return false;
  bool wrote_direct_sequences = false;
  for (auto const& [row, line] : osc_overlay_lines) {
    if (row >= static_cast<std::size_t>(LINES > 0 ? LINES : 0)) continue;
    auto const move = "\x1b[" + std::to_string(row + 1) + ";1H";
    if (std::fwrite(move.data(), 1, move.size(), stdout) != move.size()) return false;
    if (std::fwrite(line.data(), 1, line.size(), stdout) != line.size()) return false;
    wrote_direct_sequences = true;
  }
  std::vector<std::size_t> current_kitty_image_ids;
  for (auto const& graphic : frame.graphics) {
    if (graphic.protocol == TerminalImageProtocol::Kitty && graphic.image_id) {
      current_kitty_image_ids.push_back(*graphic.image_id);
    }
  }
  for (auto const image_id : active_kitty_image_ids) {
    if (std::ranges::find(current_kitty_image_ids, image_id) != current_kitty_image_ids.end()) continue;
    auto const sequence = delete_kitty_image(image_id);
    if (std::fwrite(sequence.data(), 1, sequence.size(), stdout) != sequence.size()) return false;
  }
  for (auto const& graphic : frame.graphics) {
    if (graphic.sequence.empty() || graphic.row >= static_cast<std::size_t>(LINES > 0 ? LINES : 0)) continue;
    auto const column = std::min<std::size_t>(graphic.column, COLS > 0 ? COLS - 1 : 0);
    auto const move = "\x1b[" + std::to_string(graphic.row + 1) + ";" + std::to_string(column + 1) + "H";
    if (std::fwrite(move.data(), 1, move.size(), stdout) != move.size()) return false;
    if (std::fwrite(graphic.sequence.data(), 1, graphic.sequence.size(), stdout) != graphic.sequence.size()) {
      return false;
    }
    wrote_direct_sequences = true;
  }
  if (wrote_direct_sequences && cursor_visible) {
    auto const row = std::min<std::size_t>(cursor.row, LINES > 0 ? LINES - 1 : 0);
    auto const column = std::min<std::size_t>(cursor.column, COLS > 0 ? COLS - 1 : 0);
    auto const move = "\x1b[" + std::to_string(row + 1) + ";" + std::to_string(column + 1) + "H";
    if (std::fwrite(move.data(), 1, move.size(), stdout) != move.size()) return false;
  }
  if (std::fflush(stdout) != 0) return false;
  active_kitty_image_ids = std::move(current_kitty_image_ids);
  return true;
}

}  // namespace ava::tui
