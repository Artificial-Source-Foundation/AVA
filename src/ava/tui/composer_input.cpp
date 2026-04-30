#include "ava/tui/composer_internal.h"

#include <algorithm>

namespace ava::tui::detail {
namespace {

std::string composer_bar() {
  return std::string(kSgrAccent) + std::string(kComposerBar) + std::string(kSgrReset) + std::string(kSgrComposerBg) +
         "  ";
}

std::string composer_mode_badge(std::string_view mode) {
  const auto mode_text = sanitize_terminal_text(mode);
  const auto color = mode == "build" || mode == "code" ? kSgrSuccess : kSgrAccent;
  return std::string(color) + "[" + mode_text + "]" + std::string(kSgrReset) + std::string(kSgrComposerBg);
}

std::string render_status_line(const ComposerSnapshot& snapshot, std::size_t width) {
  const auto provider = sanitize_terminal_text(snapshot.provider);
  const auto model = sanitize_terminal_text(snapshot.model);

  std::string line = composer_bar() + composer_mode_badge(snapshot.mode);
  if (!provider.empty()) {
    line += "  " + std::string(kSgrBold) + std::string(kSgrAccent) + provider + std::string(kSgrReset) +
            std::string(kSgrComposerBg);
  }
  if (!model.empty()) {
    line += "  " + std::string(kSgrMuted) + model + std::string(kSgrReset) + std::string(kSgrComposerBg);
  }
  const auto status = sanitize_terminal_text(snapshot.status);
  if (status.find("scroll") != std::string::npos || status.find("latest") != std::string::npos) {
    line += "  " + std::string(kSgrTextDimmed) + status + std::string(kSgrReset) + std::string(kSgrComposerBg);
  }

  return composer_surface_line(std::move(line), width);
}

std::string render_input_line(const ComposerSnapshot& snapshot, std::size_t width) {
  std::string line = composer_bar() + std::string(kSgrBold) + std::string(kSgrAccent) +
                     std::string(kComposerPrompt) + std::string(kSgrReset) + std::string(kSgrComposerBg) + " ";
  if (snapshot.input.empty()) {
    line += std::string(kSgrTextDimmed) + "Type a message..." + std::string(kSgrReset) +
            std::string(kSgrComposerBg);
  } else {
    line += std::string(kSgrText) + sanitize_terminal_text(snapshot.input) + std::string(kSgrReset) +
            std::string(kSgrComposerBg);
  }
  return composer_surface_line(std::move(line), width);
}

std::string render_input_fragment_line(std::string_view text, bool first_line, std::size_t width) {
  std::string line = composer_bar();
  if (first_line) {
    line += std::string(kSgrBold) + std::string(kSgrAccent) + std::string(kComposerPrompt) + std::string(kSgrReset) +
            std::string(kSgrComposerBg) + " ";
  } else {
    line += "  ";
  }
  line += std::string(kSgrText) + sanitize_terminal_text(text) + std::string(kSgrReset) + std::string(kSgrComposerBg);
  return composer_surface_line(std::move(line), width);
}

std::size_t effective_input_cursor(const ComposerSnapshot& snapshot) {
  if (snapshot.input_cursor == std::string::npos) return snapshot.input.size();
  return std::min(snapshot.input_cursor, snapshot.input.size());
}

}  // namespace

std::vector<std::string> input_render_lines(std::string_view input) {
  auto lines = split_lines(input);
  if (lines.empty()) lines.push_back("");
  return lines;
}

std::size_t composer_block_line_count(const ComposerSnapshot& snapshot, std::size_t height) {
  const auto input_lines = snapshot.input.empty() ? std::size_t{1} : input_render_lines(snapshot.input).size();
  const auto desired = std::clamp(input_lines + 2, kMinComposerBlockLines, kMaxComposerBlockLines);
  return std::min(height, desired);
}

ComposerInputLayout composer_input_layout(std::size_t input_line_count, std::size_t max_lines) {
  if (max_lines <= 1) return {.top_padding = 0, .first_visible = input_line_count > 1 ? input_line_count - 1 : 0,
                              .visible_input_lines = 1};
  const auto input_budget = max_lines - 1;
  const auto visible_input_lines = std::min(std::max<std::size_t>(input_line_count, 1), input_budget);
  const auto first_visible = input_line_count > visible_input_lines ? input_line_count - visible_input_lines : 0;
  const auto content_lines = visible_input_lines + 1;
  const auto padding = max_lines > content_lines ? max_lines - content_lines : 0;
  return {.top_padding = padding / 2, .first_visible = first_visible, .visible_input_lines = visible_input_lines};
}

std::vector<std::string> render_composer_block(const ComposerSnapshot& snapshot,
                                                std::size_t width,
                                                std::size_t max_lines) {
  if (max_lines == 0) return {};
  if (snapshot.input.empty()) {
    if (max_lines == 1) return {render_input_line(snapshot, width)};
    if (max_lines == 2) return {render_input_line(snapshot, width), render_status_line(snapshot, width)};
    std::vector<std::string> lines;
    const auto layout = composer_input_layout(1, max_lines);
    while (lines.size() < layout.top_padding) {
      lines.push_back(composer_surface_line("", width));
    }
    lines.push_back(render_input_line(snapshot, width));
    lines.push_back(render_status_line(snapshot, width));
    while (lines.size() < max_lines) {
      lines.push_back(composer_surface_line("", width));
    }
    return lines;
  }

  std::vector<std::string> lines;
  const auto input_lines = input_render_lines(snapshot.input);
  const auto layout = composer_input_layout(input_lines.size(), max_lines);
  while (lines.size() < layout.top_padding) {
    lines.push_back(composer_surface_line("", width));
  }
  const auto last_visible = std::min(input_lines.size(), layout.first_visible + layout.visible_input_lines);
  for (std::size_t index = layout.first_visible; index < last_visible; ++index) {
    lines.push_back(render_input_fragment_line(input_lines[index], index == 0, width));
  }
  if (max_lines > 1) lines.push_back(render_status_line(snapshot, width));
  while (lines.size() < max_lines) {
    lines.push_back(composer_surface_line("", width));
  }
  return lines;
}

std::size_t input_cursor_column(const ComposerSnapshot& snapshot, std::size_t width) {
  const auto cursor = effective_input_cursor(snapshot);
  const auto before_cursor = snapshot.input.substr(0, cursor);
  const auto line_start = before_cursor.rfind('\n');
  const auto line_before_cursor = line_start == std::string::npos ? before_cursor : before_cursor.substr(line_start + 1);
  const auto prefix = std::string(kComposerBar) + "  " + std::string(kComposerPrompt) + " ";
  auto column = terminal_text_columns(sanitize_terminal_text(prefix)) +
                terminal_text_columns(sanitize_terminal_text(line_before_cursor)) + 1;
  if (column == 0) column = 1;
  return std::min(column, std::max<std::size_t>(width, 1));
}

std::size_t input_cursor_line(const ComposerSnapshot& snapshot) {
  const auto cursor = effective_input_cursor(snapshot);
  return static_cast<std::size_t>(std::count(snapshot.input.begin(), snapshot.input.begin() +
                                                                    static_cast<std::ptrdiff_t>(cursor),
                                            '\n'));
}

}  // namespace ava::tui::detail
