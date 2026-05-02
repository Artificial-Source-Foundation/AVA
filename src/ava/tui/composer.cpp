#include "ava/tui/composer.h"

#include <curses.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string_view>
#include <vector>

#include "ava/tui/composer_internal.h"

namespace ava::tui {
namespace {

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

void initialize_color_pairs() {
  static bool initialized = false;
  if (initialized || !has_colors()) return;
  initialized = true;
  auto screen_bg = COLOR_BLACK;
  auto composer_bg = COLOR_BLACK;
  if (can_change_color() && COLORS > kColorComposerBg) {
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

short color_pair_for(const CursesStyle& style) {
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

attr_t curses_attributes(const CursesStyle& style) {
  if (!has_colors()) return style.attributes;
  return style.attributes | COLOR_PAIR(color_pair_for(style));
}

bool parse_sgr_codes(std::string_view sequence, std::vector<int>& codes) {
  codes.clear();
  if (sequence.empty()) return false;
  std::size_t start = 0;
  while (start <= sequence.size()) {
    const auto end = sequence.find(';', start);
    const auto token = sequence.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (token.empty()) {
      codes.push_back(0);
    } else {
      char* parsed_end = nullptr;
      const auto value = std::strtol(std::string(token).c_str(), &parsed_end, 10);
      if (parsed_end == nullptr || *parsed_end != '\0') return false;
      codes.push_back(static_cast<int>(value));
    }
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return true;
}

void apply_sgr_codes(const std::vector<int>& codes, CursesStyle& style) {
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
      case 7:
        style.attributes |= A_REVERSE;
        break;
      case 38:
        if (index + 4 < codes.size() && codes[index + 1] == 2) {
          const auto red = codes[index + 2];
          const auto green = codes[index + 3];
          const auto blue = codes[index + 4];
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
          const auto red = codes[index + 2];
          const auto green = codes[index + 3];
          const auto blue = codes[index + 4];
          style.background = (red > 15 || green > 20 || blue > 30) ? BackgroundRole::Composer : BackgroundRole::Screen;
          index += 4;
        }
        break;
      default:
        break;
    }
  }
}

void add_text_chunk(std::string_view text, CursesStyle style) {
  if (text.empty()) return;
  attrset(curses_attributes(style));
  static_cast<void>(addnstr(text.data(), static_cast<int>(std::min<std::size_t>(text.size(), INT_MAX))));
}

void draw_styled_line(std::string_view line) {
  CursesStyle style{.attributes = A_NORMAL, .color = ColorRole::Text, .background = BackgroundRole::Screen};
  std::vector<int> codes;
  std::size_t chunk_start = 0;
  for (std::size_t index = 0; index < line.size();) {
    if (line[index] != '\x1b' || index + 1 >= line.size() || line[index + 1] != '[') {
      ++index;
      continue;
    }
    const auto end = line.find('m', index + 2);
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

CursorPlacement input_cursor_placement(const ComposerSnapshot& snapshot, std::size_t rendered_line_count,
                                       std::size_t width) {
  const auto input_lines = detail::input_render_lines(snapshot.input);
  const auto composer_lines = detail::composer_block_line_count(snapshot, rendered_line_count);
  const auto layout = detail::composer_input_layout(input_lines.size(), composer_lines);
  const auto cursor_line = detail::input_cursor_line(snapshot);
  const auto visible_cursor_line =
      cursor_line < layout.first_visible ? std::size_t{0} : cursor_line - layout.first_visible;
  const auto composer_start_row =
      rendered_line_count >= composer_lines ? rendered_line_count - composer_lines : std::size_t{0};
  const auto visible_line =
      std::min(visible_cursor_line, layout.visible_input_lines == 0 ? std::size_t{0} : layout.visible_input_lines - 1);
  const auto column = detail::input_cursor_column(snapshot, width);
  return {.row = composer_start_row + layout.top_padding + visible_line,
          .column = column == 0 ? std::size_t{0} : column - 1};
}

}  // namespace

std::vector<std::string> render_composer(const ComposerSnapshot& snapshot) {
  const auto width = std::max<std::size_t>(detail::kMinWidth, snapshot.width);
  const auto height = std::max<std::size_t>(detail::kMinHeight, snapshot.height);
  std::vector<std::string> lines;
  lines.reserve(height);

  const auto normal_composer_lines = detail::composer_block_line_count(snapshot, height);
  const auto prompt_active = snapshot.permission_prompt.has_value() || snapshot.question_prompt.has_value();
  const auto fixed_lines = prompt_active ? std::size_t{0} : normal_composer_lines;
  const auto max_prompt_lines = height > fixed_lines ? height - fixed_lines : 0;
  const auto prompt_line_budget = prompt_active ? std::min<std::size_t>({7, max_prompt_lines}) : 0;
  auto permission_lines = snapshot.permission_prompt
                              ? detail::render_permission_prompt(*snapshot.permission_prompt, width, prompt_line_budget)
                              : std::vector<std::string>{};
  auto question_lines = snapshot.question_prompt
                            ? detail::render_question_prompt(*snapshot.question_prompt, width, prompt_line_budget)
                            : std::vector<std::string>{};
  const auto fixed_and_prompt_lines = fixed_lines + permission_lines.size() + question_lines.size();
  const auto palette_line_budget =
      (height > fixed_and_prompt_lines && !prompt_active && !snapshot.slash_palette_suppressed)
          ? std::min(detail::kMaxPaletteLines, height - fixed_and_prompt_lines)
          : 0;
  auto palette_lines = detail::render_slash_palette(snapshot, width, palette_line_budget);

  const auto non_transcript_lines =
      fixed_lines + palette_lines.size() + permission_lines.size() + question_lines.size();
  const auto transcript_height = height > non_transcript_lines ? height - non_transcript_lines : 0;
  const auto rendered_transcript = detail::render_transcript_lines(snapshot.transcript, width);
  const auto visible_transcript = detail::visible_transcript_lines(rendered_transcript, width, transcript_height,
                                                                   snapshot.transcript_scroll_offset);

  lines.insert(lines.end(), visible_transcript.begin(), visible_transcript.end());
  while (lines.size() < transcript_height) {
    lines.push_back("");
  }

  for (const auto& line : palette_lines) {
    lines.push_back(line);
  }
  for (const auto& line : permission_lines) {
    lines.push_back(line);
  }
  for (const auto& line : question_lines) {
    lines.push_back(line);
  }
  if (!prompt_active) {
    const auto composer_lines = detail::render_composer_block(snapshot, width, normal_composer_lines);
    lines.insert(lines.end(), composer_lines.begin(), composer_lines.end());
  }
  return lines;
}

bool draw_screen(const ComposerSnapshot& snapshot) {
  initialize_color_pairs();
  if (has_colors()) {
    static_cast<void>(
        bkgd(curses_attributes(
                 CursesStyle{.attributes = A_NORMAL, .color = ColorRole::Text, .background = BackgroundRole::Screen}) |
             ' '));
  }
  const auto width = std::max<std::size_t>(detail::kMinWidth, snapshot.width);
  const auto lines = render_composer(snapshot);

  if (snapshot.permission_prompt || snapshot.question_prompt) {
    static_cast<void>(curs_set(0));
  } else {
    static_cast<void>(curs_set(1));
  }

  erase();
  for (std::size_t index = 0; index < lines.size(); ++index) {
    if (index > static_cast<std::size_t>(LINES > 0 ? LINES - 1 : 0)) break;
    move(static_cast<int>(index), 0);
    draw_styled_line(detail::screen_surface_line(lines[index], width));
  }

  if (!snapshot.permission_prompt && !snapshot.question_prompt) {
    const auto cursor = input_cursor_placement(snapshot, lines.size(), width);
    move(static_cast<int>(std::min<std::size_t>(cursor.row, LINES > 0 ? LINES - 1 : 0)),
         static_cast<int>(std::min<std::size_t>(cursor.column, COLS > 0 ? COLS - 1 : 0)));
  }

  return refresh() != ERR;
}

}  // namespace ava::tui
