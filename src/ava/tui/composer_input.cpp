#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include "ava/config/model_profiles.h"
#include "ava/config/provider_profiles.h"
#include "ava/tui/composer_internal.h"

namespace ava::tui::detail {
namespace {

std::string composer_bar() {
  return std::string(kSgrAccent) + std::string(kComposerBar) + std::string(kSgrReset) + std::string(kSgrComposerBg) +
         "  ";
}

std::string composer_mode_badge(std::string_view mode) {
  std::string mode_text = sanitize_terminal_text(mode);
  if (!mode_text.empty())
    mode_text.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(mode_text.front())));
  auto const color = mode == "build" || mode == "code" ? kSgrWarning : kSgrAccent;
  return std::string(color) + mode_text + std::string(kSgrReset) + std::string(kSgrComposerBg);
}

std::string provider_label(std::string_view provider) {
  return sanitize_terminal_text(ava::config::provider_display_name(provider));
}

std::string model_label(std::string_view model) {
  return sanitize_terminal_text(ava::config::model_display_label(model));
}

std::string render_status_line(ComposerSnapshot const& snapshot, std::size_t width) {
  auto const provider = provider_label(snapshot.provider);
  auto const model = model_label(snapshot.model);

  std::string line = composer_bar() + composer_mode_badge(snapshot.mode);
  if (!model.empty() || !provider.empty()) {
    line += " " + std::string(kSgrMuted) + "·" + std::string(kSgrReset) + std::string(kSgrComposerBg) + " ";
    if (!model.empty()) {
      line +=
          std::string(kSgrBold) + std::string(kSgrText) + model + std::string(kSgrReset) + std::string(kSgrComposerBg);
    }
    if (!provider.empty()) {
      if (!model.empty()) line += " ";
      line += std::string(kSgrMuted) + provider + std::string(kSgrReset) + std::string(kSgrComposerBg);
    }
  }
  if (snapshot.reasoning_status && !snapshot.reasoning_status->empty()) {
    line += " " + std::string(kSgrMuted) + "·" + std::string(kSgrReset) + std::string(kSgrComposerBg) + " " +
            std::string(kSgrWarning) + sanitize_terminal_text(*snapshot.reasoning_status) + std::string(kSgrReset) +
            std::string(kSgrComposerBg);
  }
  std::string right;
  if (snapshot.token_status && !snapshot.token_status->empty()) {
    if (!right.empty()) right += "  ";
    right += std::string(kSgrMuted) + sanitize_terminal_text(*snapshot.token_status) + std::string(kSgrReset) +
             std::string(kSgrComposerBg);
  }
  if (snapshot.processing) {
    static constexpr std::array<std::string_view, 10> kSpinner = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    if (!right.empty()) right += "  ";
    right += std::string(kSgrWarning) + std::string(kSpinner[snapshot.spinner_frame % kSpinner.size()]) +
             std::string(kSgrReset) + std::string(kSgrComposerBg);
  }

  if (!right.empty()) {
    constexpr auto kRightGap = std::size_t{2};
    constexpr auto kRightMargin = std::size_t{2};
    auto const left_columns = terminal_text_columns(line);
    auto const right_columns = terminal_text_columns(right);
    if (left_columns + right_columns + kRightGap + kRightMargin <= width) {
      line += std::string(width - left_columns - right_columns - kRightMargin, ' ');
      line += right;
      line += std::string(kRightMargin, ' ');
    } else {
      line += "  " + right;
    }
  }

  return composer_surface_line(std::move(line), width);
}

std::string render_input_line(ComposerSnapshot const& snapshot, std::size_t width) {
  std::string line = composer_bar() + std::string(kSgrBold) + std::string(kSgrAccent) + std::string(kComposerPrompt) +
                     std::string(kSgrReset) + std::string(kSgrComposerBg) + " ";
  if (snapshot.input.empty()) {
    line += std::string(kSgrTextDimmed) + "Type a message..." + std::string(kSgrReset) + std::string(kSgrComposerBg);
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

std::size_t effective_input_cursor(ComposerSnapshot const& snapshot) {
  if (snapshot.input_cursor == std::string::npos) return snapshot.input.size();
  return std::min(snapshot.input_cursor, snapshot.input.size());
}

}  // namespace

std::vector<std::string> input_render_lines(std::string_view input) {
  auto lines = split_lines(input);
  if (lines.empty()) lines.push_back("");
  return lines;
}

std::size_t composer_block_line_count(ComposerSnapshot const& snapshot, std::size_t height) {
  auto const input_lines = snapshot.input.empty() ? std::size_t{1} : input_render_lines(snapshot.input).size();
  auto const desired = std::clamp(input_lines + 2, kMinComposerBlockLines, kMaxComposerBlockLines);
  return std::min(height, desired);
}

ComposerInputLayout composer_input_layout(std::size_t input_line_count, std::size_t max_lines,
                                          std::size_t draft_scroll_offset) {
  auto const effective_input_lines = std::max<std::size_t>(input_line_count, 1);
  if (max_lines <= 1) {
    auto const visible_input_lines = std::size_t{1};
    auto const max_scroll =
        effective_input_lines > visible_input_lines ? effective_input_lines - visible_input_lines : 0;
    auto const scroll = std::min(draft_scroll_offset, max_scroll);
    auto const first_visible =
        effective_input_lines > visible_input_lines ? effective_input_lines - visible_input_lines - scroll : 0;
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
  auto const first_visible =
      effective_input_lines > visible_input_lines ? effective_input_lines - visible_input_lines - scroll : 0;
  auto const content_lines = visible_input_lines + 1;
  auto const padding = max_lines > content_lines ? max_lines - content_lines : 0;
  return {.top_padding = padding / 2,
          .first_visible = first_visible,
          .visible_input_lines = visible_input_lines,
          .hidden_above = first_visible,
          .hidden_below = effective_input_lines - first_visible - visible_input_lines};
}

std::vector<std::string> render_composer_block(ComposerSnapshot const& snapshot, std::size_t width,
                                               std::size_t max_lines) {
  if (max_lines == 0) return {};
  if (snapshot.input.empty()) {
    if (max_lines == 1) return {render_input_line(snapshot, width)};
    if (max_lines == 2) return {render_input_line(snapshot, width), render_status_line(snapshot, width)};
    std::vector<std::string> lines;
    auto const layout = composer_input_layout(1, max_lines, 0);
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
  auto const input_lines = input_render_lines(snapshot.input);
  auto const layout = composer_input_layout(input_lines.size(), max_lines, snapshot.draft_scroll_offset);
  while (lines.size() < layout.top_padding) {
    lines.push_back(composer_surface_line("", width));
  }
  auto const last_visible = std::min(input_lines.size(), layout.first_visible + layout.visible_input_lines);
  for (std::size_t index = layout.first_visible; index < last_visible; ++index) {
    lines.push_back(render_input_fragment_line(input_lines[index], index == 0, width));
  }
  if (max_lines > 1) lines.push_back(render_status_line(snapshot, width));
  while (lines.size() < max_lines) {
    lines.push_back(composer_surface_line("", width));
  }
  return lines;
}

std::size_t input_cursor_column(ComposerSnapshot const& snapshot, std::size_t width) {
  auto const cursor = effective_input_cursor(snapshot);
  auto const before_cursor = snapshot.input.substr(0, cursor);
  auto const line_start = before_cursor.rfind('\n');
  auto const line_before_cursor =
      line_start == std::string::npos ? before_cursor : before_cursor.substr(line_start + 1);
  auto const prefix = std::string(kComposerBar) + "  " + std::string(kComposerPrompt) + " ";
  auto column = terminal_text_columns(sanitize_terminal_text(prefix)) +
                terminal_text_columns(sanitize_terminal_text(line_before_cursor)) + 1;
  if (column == 0) column = 1;
  return std::min(column, std::max<std::size_t>(width, 1));
}

std::size_t input_cursor_line(ComposerSnapshot const& snapshot) {
  auto const cursor = effective_input_cursor(snapshot);
  return static_cast<std::size_t>(
      std::count(snapshot.input.begin(), snapshot.input.begin() + static_cast<std::ptrdiff_t>(cursor), '\n'));
}

}  // namespace ava::tui::detail
