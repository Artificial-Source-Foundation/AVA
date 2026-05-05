#include "ava/tui/composer_internal.h"

#include <algorithm>
#include <cctype>

namespace ava::tui {
namespace detail {

std::string slash_command_prefix(std::string_view input)
{
  if (!input.starts_with('/')) return {};
  auto const end = input.find_first_of(" \t\r\n");
  auto prefix = input.substr(1, end == std::string_view::npos ? std::string_view::npos : end - 1);
  return std::string(prefix);
}

namespace {

std::string_view command_token(std::string_view input)
{
  auto const end = input.find_first_of(" \t\r\n");
  return input.substr(0, end == std::string_view::npos ? input.size() : end);
}

std::string_view argument_text(std::string_view input)
{
  auto const start = input.find_first_of(" \t\r\n");
  if (start == std::string_view::npos) return {};
  return input.substr(start + 1);
}

bool has_argument_text(std::string_view input)
{
  return input.find_first_of(" \t\r\n") != std::string_view::npos;
}

std::vector<std::string> split_argument_tokens(std::string_view text)
{
  std::vector<std::string> tokens;
  std::size_t index = 0;
  while (index < text.size()) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) ++index;
    auto const start = index;
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) == 0) ++index;
    if (start < index) tokens.emplace_back(text.substr(start, index - start));
  }
  return tokens;
}

bool ends_with_ascii_space(std::string_view text)
{
  if (text.empty()) return false;
  auto const byte = static_cast<unsigned char>(text.back());
  return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

std::size_t current_argument_index(std::string_view text, std::vector<std::string> const& tokens)
{
  return ends_with_ascii_space(text) ? tokens.size() : tokens.empty() ? std::size_t{0} : tokens.size() - 1;
}

std::string current_argument_prefix(std::string_view text, std::vector<std::string> const& tokens)
{
  if (ends_with_ascii_space(text) || tokens.empty()) return {};
  return tokens.back();
}

bool slash_command_matches(std::string_view command, std::string_view prefix)
{
  if (command.starts_with('/')) command.remove_prefix(1);
  return prefix.empty() || command.starts_with(prefix);
}

bool slash_command_item_matches(SlashCommandItem const& item, std::string_view prefix)
{
  if (slash_command_matches(item.command, prefix)) return true;
  return std::ranges::any_of(item.aliases,
                             [&](std::string_view alias) { return slash_command_matches(alias, prefix); });
}

bool slash_command_name_exact_match(std::string_view command, std::string_view prefix)
{
  if (command.starts_with('/')) command.remove_prefix(1);
  return command == prefix;
}

bool slash_command_exact_match(SlashCommandItem const& item, std::string_view prefix)
{
  if (slash_command_name_exact_match(item.command, prefix)) return true;
  return std::ranges::any_of(item.aliases,
                             [&](std::string_view alias) { return slash_command_name_exact_match(alias, prefix); });
}

bool slash_command_token_exact_match(SlashCommandItem const& item, std::string_view token)
{
  return item.command == token || std::ranges::find(item.aliases, token) != item.aliases.end();
}

SlashCommandItem const* find_slash_command_for_arguments(std::string_view input,
                                                         std::vector<SlashCommandItem> const& commands)
{
  auto const token = command_token(input);
  if (token.empty()) return nullptr;
  for (auto const& command : commands) {
    if (slash_command_token_exact_match(command, token)) return &command;
  }
  return nullptr;
}

bool completion_previous_args_match(SlashCommandArgumentCompletion const& completion,
                                    std::vector<std::string> const& tokens)
{
  if (completion.required_previous_args.empty()) return true;
  if (tokens.size() < completion.required_previous_args.size()) return false;
  for (std::size_t index = 0; index < completion.required_previous_args.size(); ++index) {
    if (tokens[index] != completion.required_previous_args[index]) return false;
  }
  return true;
}

std::string completion_insert_text(SlashCommandItem const& command, SlashCommandArgumentCompletion const& completion,
                                   std::vector<std::string> const& tokens, std::size_t argument_index)
{
  std::string text = command.command;
  std::vector<std::string> next_tokens;
  next_tokens.reserve(std::max(tokens.size(), argument_index + 1));
  for (std::size_t index = 0; index < argument_index && index < tokens.size(); ++index) {
    next_tokens.push_back(tokens[index]);
  }
  next_tokens.push_back(completion.value);
  for (auto const& token : next_tokens) {
    text += " " + token;
  }
  if (completion.append_space) text.push_back(' ');
  return text;
}

bool slash_command_has_argument_completions(std::string_view input, std::vector<SlashCommandItem> const& commands)
{
  auto const* command = find_slash_command_for_arguments(input, commands);
  return command != nullptr && !command->argument_completions.empty();
}

std::string slash_command_display(SlashCommandItem const& item)
{
  if (item.argument_completion) return item.command;
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

std::string slash_command_hint_display(SlashCommandItem const& item)
{
  if (item.argument_completion) return item.hint;
  auto text = item.hint;
  if (!item.key_display.empty()) {
    if (!text.empty()) text += " · ";
    text += item.key_display;
  }
  return text;
}

std::string slash_command_description_display(SlashCommandItem const& item)
{
  auto text = item.description;
  if (!item.enabled) {
    if (!text.empty()) text += " — ";
    text += "disabled";
    if (!item.disabled_reason.empty()) text += ": " + item.disabled_reason;
  }
  return text;
}

std::vector<SlashCommandItem> filter_slash_argument_completions(std::string_view input,
                                                                std::vector<SlashCommandItem> const& commands)
{
  std::vector<SlashCommandItem> matches;
  auto const* command = find_slash_command_for_arguments(input, commands);
  if (!command || command->argument_completions.empty()) return matches;

  auto const args_text = argument_text(input);
  auto const tokens = split_argument_tokens(args_text);
  auto const argument_index = current_argument_index(args_text, tokens);
  auto const prefix = current_argument_prefix(args_text, tokens);
  for (auto const& completion : command->argument_completions) {
    if (completion.argument_index != argument_index) continue;
    if (!completion_previous_args_match(completion, tokens)) continue;
    if (!prefix.empty() && !completion.value.starts_with(prefix)) continue;
    matches.push_back(SlashCommandItem{
        .command = completion.value,
        .description = completion.description,
        .hint = completion.append_space ? "" : "[complete]",
        .category = completion.category.empty() ? command->category : completion.category,
        .enabled = command->enabled && completion.enabled,
        .disabled_reason = command->enabled ? completion.disabled_reason : command->disabled_reason,
        .argument_completion = true,
        .completion_insert_text = completion_insert_text(*command, completion, tokens, argument_index)});
  }
  return matches;
}

std::string palette_prefix()
{
  return std::string(kSgrAccent) + std::string(kComposerBar) + std::string(kSgrReset) + std::string(kSgrComposerBg) +
         "  ";
}

std::size_t palette_content_width(std::size_t width)
{
  auto const prefix_cols = detail::terminal_text_columns(palette_prefix());
  return width > prefix_cols ? width - prefix_cols : width;
}

std::string palette_surface_line(std::string content, std::size_t width)
{
  return detail::composer_surface_line(palette_prefix() + std::move(content), width);
}

std::string render_palette_item_columns(SlashCommandItem const& item, bool selected, std::size_t selected_index,
                                        std::size_t match_count, std::size_t width, std::size_t cmd_col_width,
                                        std::size_t category_col_width, std::size_t hint_col_width)
{
  static_cast<void>(selected_index);
  static_cast<void>(match_count);
  auto command_text = slash_command_display(item);
  auto const hint_text = slash_command_hint_display(item);
  auto const description_text = slash_command_description_display(item);

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

std::string render_palette_item_compact(SlashCommandItem const& item, bool selected, std::size_t selected_index,
                                        std::size_t match_count, std::size_t width)
{
  static_cast<void>(selected_index);
  static_cast<void>(match_count);
  std::string line = selected ? "› " : "  ";
  line += slash_command_display(item);
  auto const hint_text = slash_command_hint_display(item);
  if (!hint_text.empty()) {
    line += " " + hint_text;
  }
  if (!item.category.empty()) {
    line += "  [" + item.category + ']';
  }
  auto const description_text = slash_command_description_display(item);
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

std::vector<std::string> render_slash_palette(ComposerSnapshot const& snapshot, std::size_t width,
                                              std::size_t max_lines)
{
  std::vector<std::string> lines;
  if (max_lines == 0) return lines;
  auto const matches = filter_slash_commands(snapshot.input, snapshot.slash_commands);
  if (!slash_palette_visible(snapshot.input, snapshot.slash_commands)) return lines;

  auto const prefix = slash_command_prefix(snapshot.input);
  auto const selected =
      clamp_slash_palette_selection(snapshot.input, snapshot.slash_commands, snapshot.selected_slash_command_index);

  auto const item_budget = max_lines;

  if (matches.empty()) {
    if (lines.size() < max_lines) {
      auto const text = has_argument_text(snapshot.input) ? std::string("  no matching arguments")
                        : prefix.empty()                  ? std::string("  no matching commands")
                                                          : "  no commands match /" + prefix;
      lines.push_back(palette_surface_line(text, width));
    }
    return lines;
  }

  auto const visible_items = std::min(matches.size(), item_budget);
  auto start = selected >= visible_items ? selected - visible_items + 1 : 0;
  if (start + visible_items > matches.size()) start = matches.size() - visible_items;

  std::size_t max_cmd_cols = 0;
  std::size_t max_category_cols = 0;
  std::size_t max_hint_cols = 0;
  bool has_any_hint = false;
  bool has_any_category = false;
  for (std::size_t offset = 0; offset < visible_items; ++offset) {
    auto const& item = matches[start + offset];
    auto command_text = slash_command_display(item);
    max_cmd_cols = std::max(max_cmd_cols, detail::terminal_text_columns(command_text));
    if (!item.category.empty()) {
      has_any_category = true;
      max_category_cols = std::max(max_category_cols, detail::terminal_text_columns(item.category));
    }
    auto const hint_text = slash_command_hint_display(item);
    if (!hint_text.empty()) {
      has_any_hint = true;
      max_hint_cols = std::max(max_hint_cols, detail::terminal_text_columns(hint_text));
    }
  }

  bool const use_columns = width >= 40 && (2 + max_cmd_cols + (has_any_category ? max_category_cols + 2 : 0) +
                                               (has_any_hint ? max_hint_cols + 2 : 0) + 4 <=
                                           width);

  for (std::size_t offset = 0; offset < visible_items && lines.size() < max_lines; ++offset) {
    auto const index = start + offset;
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
                                                    std::vector<SlashCommandItem> const& commands)
{
  std::vector<SlashCommandItem> matches;
  if (!input.starts_with('/')) return matches;
  if (detail::has_argument_text(input)) return detail::filter_slash_argument_completions(input, commands);

  auto const prefix = detail::slash_command_prefix(input);
  for (auto const& command : commands) {
    if (detail::slash_command_item_matches(command, prefix)) {
      matches.push_back(command);
    }
  }
  return matches;
}

bool slash_palette_visible(std::string_view input, std::vector<SlashCommandItem> const& commands)
{
  if (!input.starts_with('/') || commands.empty()) return false;
  if (detail::has_argument_text(input)) return detail::slash_command_has_argument_completions(input, commands);

  auto const prefix = detail::slash_command_prefix(input);
  for (auto const& command : commands) {
    if (detail::slash_command_exact_match(command, prefix) && command.hint.empty() && command.enabled) return false;
  }
  return true;
}

std::size_t clamp_slash_palette_selection(std::string_view input, std::vector<SlashCommandItem> const& commands,
                                          std::size_t selected_index)
{
  auto const matches = filter_slash_commands(input, commands);
  if (matches.empty()) return 0;
  return std::min(selected_index, matches.size() - 1);
}

std::size_t previous_slash_palette_selection(std::string_view input, std::vector<SlashCommandItem> const& commands,
                                             std::size_t selected_index)
{
  auto const matches = filter_slash_commands(input, commands);
  if (matches.empty()) return 0;
  auto const selected = std::min(selected_index, matches.size() - 1);
  return selected == 0 ? matches.size() - 1 : selected - 1;
}

std::size_t next_slash_palette_selection(std::string_view input, std::vector<SlashCommandItem> const& commands,
                                         std::size_t selected_index)
{
  auto const matches = filter_slash_commands(input, commands);
  if (matches.empty()) return 0;
  auto const selected = std::min(selected_index, matches.size() - 1);
  return (selected + 1) % matches.size();
}

std::string slash_command_selection_text(std::string_view input, std::vector<SlashCommandItem> const& commands,
                                         std::size_t selected_index)
{
  auto const matches = filter_slash_commands(input, commands);
  if (matches.empty()) return std::string(input);
  auto const selected = std::min(selected_index, matches.size() - 1);
  if (matches[selected].argument_completion) return matches[selected].completion_insert_text;
  auto text = matches[selected].command;
  if (!matches[selected].hint.empty()) text.push_back(' ');
  return text;
}

std::optional<std::string> slash_command_selection_disabled_reason(std::string_view input,
                                                                   std::vector<SlashCommandItem> const& commands,
                                                                   std::size_t selected_index)
{
  auto const matches = filter_slash_commands(input, commands);
  if (matches.empty()) return std::nullopt;
  auto const selected = std::min(selected_index, matches.size() - 1);
  if (matches[selected].enabled) return std::nullopt;
  if (!matches[selected].disabled_reason.empty()) return matches[selected].disabled_reason;
  return std::string("command is disabled");
}

std::optional<std::size_t> slash_palette_selection_for_screen_row(ComposerSnapshot const& snapshot, std::size_t row)
{
  if (row == 0 || snapshot.permission_prompt || snapshot.question_prompt || snapshot.slash_palette_suppressed ||
      !slash_palette_visible(snapshot.input, snapshot.slash_commands)) {
    return std::nullopt;
  }
  auto const height = std::max<std::size_t>(detail::kMinHeight, snapshot.height);
  auto const matches = filter_slash_commands(snapshot.input, snapshot.slash_commands);
  if (matches.empty()) return std::nullopt;

  auto const fixed_lines = detail::composer_block_line_count(snapshot, height);
  auto const palette_line_budget = height > fixed_lines ? std::min(detail::kMaxPaletteLines, height - fixed_lines) : 0;
  if (palette_line_budget == 0) return std::nullopt;

  auto const item_budget = palette_line_budget;
  auto const visible_items = std::min(matches.size(), item_budget);
  if (visible_items == 0) return std::nullopt;

  auto const selected =
      clamp_slash_palette_selection(snapshot.input, snapshot.slash_commands, snapshot.selected_slash_command_index);
  auto start = selected >= visible_items ? selected - visible_items + 1 : 0;
  if (start + visible_items > matches.size()) start = matches.size() - visible_items;

  auto const palette_lines = visible_items;
  auto const non_transcript_lines = fixed_lines + palette_lines;
  auto const transcript_height = height > non_transcript_lines ? height - non_transcript_lines : 0;
  auto const first_item_row = transcript_height + 1;
  if (row < first_item_row) return std::nullopt;
  auto const item_offset = row - first_item_row;
  if (item_offset >= visible_items) return std::nullopt;
  return start + item_offset;
}

}  // namespace ava::tui
