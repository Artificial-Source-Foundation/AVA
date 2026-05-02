#include <algorithm>

#include "ava/tui/composer_internal.h"

namespace ava::tui {
namespace detail {

std::string slash_command_prefix(std::string_view input) {
  if (!input.starts_with('/')) return {};
  const auto end = input.find_first_of(" \t\r\n");
  auto prefix = input.substr(1, end == std::string_view::npos ? std::string_view::npos : end - 1);
  return std::string(prefix);
}

namespace {

bool slash_command_matches(std::string_view command, std::string_view prefix) {
  if (command.starts_with('/')) command.remove_prefix(1);
  return prefix.empty() || command.starts_with(prefix);
}

bool slash_command_item_matches(const SlashCommandItem& item, std::string_view prefix) {
  if (slash_command_matches(item.command, prefix)) return true;
  return std::ranges::any_of(item.aliases,
                             [&](std::string_view alias) { return slash_command_matches(alias, prefix); });
}

bool slash_command_name_exact_match(std::string_view command, std::string_view prefix) {
  if (command.starts_with('/')) command.remove_prefix(1);
  return command == prefix;
}

bool slash_command_exact_match(const SlashCommandItem& item, std::string_view prefix) {
  if (slash_command_name_exact_match(item.command, prefix)) return true;
  return std::ranges::any_of(item.aliases,
                             [&](std::string_view alias) { return slash_command_name_exact_match(alias, prefix); });
}

std::string slash_command_display(const SlashCommandItem& item) {
  auto text = item.command;
  if (!item.aliases.empty()) {
    text += " (";
    for (std::size_t index = 0; index < item.aliases.size(); ++index) {
      if (index > 0) text += ", ";
      text += item.aliases[index];
    }
    text += ')';
  }
  return text;
}

std::string slash_command_hint_display(const SlashCommandItem& item) {
  auto text = item.hint;
  if (!item.key_display.empty()) {
    if (!text.empty()) text += " · ";
    text += item.key_display;
  }
  return text;
}

std::string slash_command_description_display(const SlashCommandItem& item) {
  auto text = item.description;
  if (!item.enabled) {
    if (!text.empty()) text += " — ";
    text += "disabled";
    if (!item.disabled_reason.empty()) text += ": " + item.disabled_reason;
  }
  return text;
}

std::string palette_prefix() {
  return std::string(kSgrAccent) + std::string(kComposerBar) + std::string(kSgrReset) + std::string(kSgrComposerBg) +
         "  ";
}

std::size_t palette_content_width(std::size_t width) {
  const auto prefix_cols = detail::terminal_text_columns(palette_prefix());
  return width > prefix_cols ? width - prefix_cols : width;
}

std::string palette_surface_line(std::string content, std::size_t width) {
  return detail::composer_surface_line(palette_prefix() + std::move(content), width);
}

std::string render_palette_item_columns(const SlashCommandItem& item, bool selected, std::size_t selected_index,
                                        std::size_t match_count, std::size_t width, std::size_t cmd_col_width,
                                        std::size_t category_col_width, std::size_t hint_col_width) {
  static_cast<void>(selected_index);
  static_cast<void>(match_count);
  auto command_text = slash_command_display(item);
  const auto hint_text = slash_command_hint_display(item);
  const auto description_text = slash_command_description_display(item);

  std::string line = selected ? "› " : "  ";
  line += command_text;

  auto cmd_cols = detail::terminal_text_columns(command_text);
  if (cmd_cols < cmd_col_width && detail::terminal_text_columns(line) + (cmd_col_width - cmd_cols) <= width) {
    line += std::string(cmd_col_width - cmd_cols, ' ');
  }

  if (!item.category.empty()) {
    line += "  " + item.category;
    auto category_cols = detail::terminal_text_columns(item.category);
    if (category_cols < category_col_width &&
        detail::terminal_text_columns(line) + (category_col_width - category_cols) <= width) {
      line += std::string(category_col_width - category_cols, ' ');
    }
  }

  if (!hint_text.empty()) {
    line += "  " + hint_text;
    auto hint_cols = detail::terminal_text_columns(hint_text);
    if (hint_cols < hint_col_width && detail::terminal_text_columns(line) + (hint_col_width - hint_cols) <= width) {
      line += std::string(hint_col_width - hint_cols, ' ');
    }
  }

  if (!description_text.empty()) {
    line += "  " + description_text;
  }

  line = detail::fit_line(std::move(line), palette_content_width(width));
  if (selected) {
    line = std::string(kReverseVideo) + line + std::string(kSgrReset) + std::string(kSgrComposerBg);
  }
  if (!item.enabled && !selected)
    line = std::string(kSgrDim) + line + std::string(kSgrReset) + std::string(kSgrComposerBg);
  return palette_surface_line(std::move(line), width);
}

std::string render_palette_item_compact(const SlashCommandItem& item, bool selected, std::size_t selected_index,
                                        std::size_t match_count, std::size_t width) {
  static_cast<void>(selected_index);
  static_cast<void>(match_count);
  std::string line = selected ? "› " : "  ";
  line += slash_command_display(item);
  const auto hint_text = slash_command_hint_display(item);
  if (!hint_text.empty()) {
    line += " " + hint_text;
  }
  if (!item.category.empty()) {
    line += "  [" + item.category + ']';
  }
  const auto description_text = slash_command_description_display(item);
  if (!description_text.empty()) {
    line += "  " + description_text;
  }
  line = detail::fit_line(std::move(line), palette_content_width(width));
  if (selected) {
    line = std::string(kReverseVideo) + line + std::string(kSgrReset) + std::string(kSgrComposerBg);
  }
  if (!item.enabled && !selected)
    line = std::string(kSgrDim) + line + std::string(kSgrReset) + std::string(kSgrComposerBg);
  return palette_surface_line(std::move(line), width);
}

}  // namespace

std::vector<std::string> render_slash_palette(const ComposerSnapshot& snapshot, std::size_t width,
                                              std::size_t max_lines) {
  std::vector<std::string> lines;
  if (max_lines == 0) return lines;
  const auto matches = filter_slash_commands(snapshot.input, snapshot.slash_commands);
  if (!slash_palette_visible(snapshot.input, snapshot.slash_commands)) return lines;

  const auto prefix = slash_command_prefix(snapshot.input);
  const auto selected =
      clamp_slash_palette_selection(snapshot.input, snapshot.slash_commands, snapshot.selected_slash_command_index);

  const auto item_budget = max_lines;

  if (matches.empty()) {
    if (lines.size() < max_lines) {
      lines.push_back(
          palette_surface_line(prefix.empty() ? "  no matching commands" : "  no commands match /" + prefix, width));
    }
    return lines;
  }

  const auto visible_items = std::min(matches.size(), item_budget);
  auto start = selected >= visible_items ? selected - visible_items + 1 : 0;
  if (start + visible_items > matches.size()) start = matches.size() - visible_items;

  std::size_t max_cmd_cols = 0;
  std::size_t max_category_cols = 0;
  std::size_t max_hint_cols = 0;
  bool has_any_hint = false;
  bool has_any_category = false;
  for (std::size_t offset = 0; offset < visible_items; ++offset) {
    const auto& item = matches[start + offset];
    auto command_text = slash_command_display(item);
    max_cmd_cols = std::max(max_cmd_cols, detail::terminal_text_columns(command_text));
    if (!item.category.empty()) {
      has_any_category = true;
      max_category_cols = std::max(max_category_cols, detail::terminal_text_columns(item.category));
    }
    const auto hint_text = slash_command_hint_display(item);
    if (!hint_text.empty()) {
      has_any_hint = true;
      max_hint_cols = std::max(max_hint_cols, detail::terminal_text_columns(hint_text));
    }
  }

  const bool use_columns = width >= 40 && (2 + max_cmd_cols + (has_any_category ? max_category_cols + 2 : 0) +
                                               (has_any_hint ? max_hint_cols + 2 : 0) + 4 <=
                                           width);

  for (std::size_t offset = 0; offset < visible_items && lines.size() < max_lines; ++offset) {
    const auto index = start + offset;
    if (use_columns) {
      lines.push_back(render_palette_item_columns(matches[index], index == selected, selected, matches.size(), width,
                                                  max_cmd_cols, max_category_cols, max_hint_cols));
    } else {
      lines.push_back(render_palette_item_compact(matches[index], index == selected, selected, matches.size(), width));
    }
  }

  return lines;
}

}  // namespace detail

std::vector<SlashCommandItem> filter_slash_commands(std::string_view input,
                                                    const std::vector<SlashCommandItem>& commands) {
  std::vector<SlashCommandItem> matches;
  if (!input.starts_with('/')) return matches;

  const auto prefix = detail::slash_command_prefix(input);
  for (const auto& command : commands) {
    if (detail::slash_command_item_matches(command, prefix)) {
      matches.push_back(command);
    }
  }
  return matches;
}

bool slash_palette_visible(std::string_view input, const std::vector<SlashCommandItem>& commands) {
  if (!input.starts_with('/') || commands.empty()) return false;
  if (input.find_first_of(" \t\r\n") != std::string_view::npos) return false;

  const auto prefix = detail::slash_command_prefix(input);
  for (const auto& command : commands) {
    if (detail::slash_command_exact_match(command, prefix) && command.hint.empty() && command.enabled) return false;
  }
  return true;
}

std::size_t clamp_slash_palette_selection(std::string_view input, const std::vector<SlashCommandItem>& commands,
                                          std::size_t selected_index) {
  const auto matches = filter_slash_commands(input, commands);
  if (matches.empty()) return 0;
  return std::min(selected_index, matches.size() - 1);
}

std::size_t previous_slash_palette_selection(std::string_view input, const std::vector<SlashCommandItem>& commands,
                                             std::size_t selected_index) {
  const auto matches = filter_slash_commands(input, commands);
  if (matches.empty()) return 0;
  const auto selected = std::min(selected_index, matches.size() - 1);
  return selected == 0 ? matches.size() - 1 : selected - 1;
}

std::size_t next_slash_palette_selection(std::string_view input, const std::vector<SlashCommandItem>& commands,
                                         std::size_t selected_index) {
  const auto matches = filter_slash_commands(input, commands);
  if (matches.empty()) return 0;
  const auto selected = std::min(selected_index, matches.size() - 1);
  return (selected + 1) % matches.size();
}

std::string slash_command_selection_text(std::string_view input, const std::vector<SlashCommandItem>& commands,
                                         std::size_t selected_index) {
  const auto matches = filter_slash_commands(input, commands);
  if (matches.empty()) return std::string(input);
  const auto selected = std::min(selected_index, matches.size() - 1);
  auto text = matches[selected].command;
  if (!matches[selected].hint.empty()) text.push_back(' ');
  return text;
}

std::optional<std::string> slash_command_selection_disabled_reason(std::string_view input,
                                                                   const std::vector<SlashCommandItem>& commands,
                                                                   std::size_t selected_index) {
  const auto matches = filter_slash_commands(input, commands);
  if (matches.empty()) return std::nullopt;
  const auto selected = std::min(selected_index, matches.size() - 1);
  if (matches[selected].enabled) return std::nullopt;
  if (!matches[selected].disabled_reason.empty()) return matches[selected].disabled_reason;
  return std::string("command is disabled");
}

std::optional<std::size_t> slash_palette_selection_for_screen_row(const ComposerSnapshot& snapshot, std::size_t row) {
  if (row == 0 || snapshot.permission_prompt || snapshot.question_prompt || snapshot.slash_palette_suppressed ||
      !slash_palette_visible(snapshot.input, snapshot.slash_commands)) {
    return std::nullopt;
  }
  const auto height = std::max<std::size_t>(detail::kMinHeight, snapshot.height);
  const auto matches = filter_slash_commands(snapshot.input, snapshot.slash_commands);
  if (matches.empty()) return std::nullopt;

  const auto fixed_lines = detail::composer_block_line_count(snapshot, height);
  const auto palette_line_budget = height > fixed_lines ? std::min(detail::kMaxPaletteLines, height - fixed_lines) : 0;
  if (palette_line_budget == 0) return std::nullopt;

  const auto item_budget = palette_line_budget;
  const auto visible_items = std::min(matches.size(), item_budget);
  if (visible_items == 0) return std::nullopt;

  const auto selected =
      clamp_slash_palette_selection(snapshot.input, snapshot.slash_commands, snapshot.selected_slash_command_index);
  auto start = selected >= visible_items ? selected - visible_items + 1 : 0;
  if (start + visible_items > matches.size()) start = matches.size() - visible_items;

  const auto palette_lines = visible_items;
  const auto non_transcript_lines = fixed_lines + palette_lines;
  const auto transcript_height = height > non_transcript_lines ? height - non_transcript_lines : 0;
  const auto first_item_row = transcript_height + 1;
  if (row < first_item_row) return std::nullopt;
  const auto item_offset = row - first_item_row;
  if (item_offset >= visible_items) return std::nullopt;
  return start + item_offset;
}

}  // namespace ava::tui
