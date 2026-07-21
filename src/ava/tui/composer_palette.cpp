#include "sys.h"
#include "ava/tui/composer_internal.h"

#include <algorithm>
#include <cctype>

namespace ava::tui {
namespace detail {

std::string slash_command_prefix(std::string_view input)
{
  if (!input.starts_with('/'))
    return {};
  auto const end = input.find_first_of(" \t\r\n");
  auto prefix = input.substr(1, end == std::string_view::npos ? std::string_view::npos : end - 1);
  return std::string(prefix);
}

namespace {

struct FileReferencePrefix
{
  std::size_t start = 0;
  std::size_t cursor = 0;
  std::string value = {};
  bool quoted = false;
};

struct PathCompletionPrefix
{
  std::size_t start = 0;
  std::size_t cursor = 0;
  std::string value = {};
  bool quoted = false;
  bool leading_dot_slash = false;
};

struct ScoredFileReference
{
  FileReferenceItem item;
  int score = 0;
};

struct ScoredSlashCommand
{
  SlashCommandItem item;
  int score = 0;
  std::size_t order = 0;
};

struct ScoredArgumentCompletion
{
  SlashCommandArgumentCompletion completion;
  int score = 0;
  std::size_t order = 0;
};

std::string_view command_token(std::string_view input)
{
  auto const end = input.find_first_of(" \t\r\n");
  return input.substr(0, end == std::string_view::npos ? input.size() : end);
}

std::size_t effective_cursor(std::string_view input, std::size_t cursor)
{
  return cursor == std::string::npos ? input.size() : std::min(cursor, input.size());
}

std::string_view input_before_cursor(std::string_view input, std::size_t cursor)
{
  return input.substr(0, effective_cursor(input, cursor));
}

std::string_view argument_text(std::string_view input)
{
  auto const start = input.find_first_of(" \t\r\n");
  if (start == std::string_view::npos)
    return {};
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
  while (index < text.size())
  {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) ++index;
    auto const start = index;
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) == 0) ++index;
    if (start < index)
      tokens.emplace_back(text.substr(start, index - start));
  }
  return tokens;
}

bool ends_with_ascii_space(std::string_view text)
{
  if (text.empty())
    return false;
  auto const byte = static_cast<unsigned char>(text.back());
  return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

bool is_ascii_space(char ch)
{
  auto const byte = static_cast<unsigned char>(ch);
  return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

bool is_path_completion_delimiter(char ch)
{
  return is_ascii_space(ch) || ch == '"' || ch == '\'' || ch == '=';
}

bool is_file_reference_boundary_char(char ch)
{
  return is_path_completion_delimiter(ch) || ch == '(' || ch == '[' || ch == '{';
}

bool is_file_reference_closing_boundary(char ch)
{
  return ch == ')' || ch == ']' || ch == '}' || ch == ',' || ch == '.' || ch == ';' || ch == ':';
}

bool is_file_reference_boundary(std::string_view input, std::size_t index)
{
  return index == 0 || is_file_reference_boundary_char(input[index - 1]);
}

std::string ascii_lower(std::string_view text)
{
  std::string lowered;
  lowered.reserve(text.size());
  for (auto ch : text)
  {
    lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return lowered;
}

std::vector<std::string> file_reference_query_tokens(std::string_view query)
{
  std::vector<std::string> tokens;
  std::size_t index = 0;
  while (index < query.size())
  {
    while (index < query.size() && (is_ascii_space(query[index]) || query[index] == '/')) ++index;
    auto const start = index;
    while (index < query.size() && !is_ascii_space(query[index]) && query[index] != '/') ++index;
    if (start < index)
      tokens.emplace_back(query.substr(start, index - start));
  }
  return tokens;
}

std::optional<std::string> swapped_alpha_numeric_query(std::string_view query)
{
  if (query.empty())
    return std::nullopt;

  auto const first_digit = std::find_if(query.begin(), query.end(), [](char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
  });
  auto const first_alpha = std::find_if(query.begin(), query.end(), [](char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0;
  });
  if (first_digit == query.end() || first_alpha == query.end())
    return std::nullopt;

  auto const all_alpha = [](std::string_view value) {
    return !value.empty() && std::ranges::all_of(value, [](char ch) {
             return std::isalpha(static_cast<unsigned char>(ch)) != 0;
           });
  };
  auto const all_digit = [](std::string_view value) {
    return !value.empty() && std::ranges::all_of(value, [](char ch) {
             return std::isdigit(static_cast<unsigned char>(ch)) != 0;
           });
  };

  auto const digit_index = static_cast<std::size_t>(std::distance(query.begin(), first_digit));
  if (digit_index > 0)
  {
    auto const letters = query.substr(0, digit_index);
    auto const digits = query.substr(digit_index);
    if (all_alpha(letters) && all_digit(digits))
    {
      std::string swapped(digits);
      swapped += letters;
      return swapped;
    }
  }

  auto const alpha_index = static_cast<std::size_t>(std::distance(query.begin(), first_alpha));
  if (alpha_index > 0)
  {
    auto const digits = query.substr(0, alpha_index);
    auto const letters = query.substr(alpha_index);
    if (all_digit(digits) && all_alpha(letters))
    {
      std::string swapped(letters);
      swapped += digits;
      return swapped;
    }
  }

  return std::nullopt;
}

std::optional<int> fuzzy_ordered_text_score(std::string_view lowered_query, std::string_view lowered_text)
{
  if (lowered_query.empty())
    return 0;
  if (lowered_query.size() > lowered_text.size())
    return std::nullopt;
  if (lowered_text == lowered_query)
    return -1000;
  if (lowered_text.starts_with(lowered_query))
    return -500 + static_cast<int>(lowered_query.size());
  if (auto const substring = lowered_text.find(lowered_query); substring != std::string::npos)
    return static_cast<int>(substring * 4);

  std::size_t text_index = 0;
  int score = 0;
  int consecutive = 0;
  int last_match = -1;
  for (std::size_t query_index = 0; query_index < lowered_query.size(); ++query_index)
  {
    auto const wanted = lowered_query[query_index];
    auto matched = false;
    while (text_index < lowered_text.size())
    {
      if (lowered_text[text_index] == wanted)
      {
        auto const current = static_cast<int>(text_index);
        if (last_match + 1 == current)
        {
          ++consecutive;
          score -= 8 * consecutive;
        }
        else
        {
          consecutive = 0;
          if (last_match >= 0)
            score += (current - last_match - 1) * 3;
        }
        if (text_index == 0 || lowered_text[text_index - 1] == '/' || lowered_text[text_index - 1] == '-' ||
            lowered_text[text_index - 1] == '_' || lowered_text[text_index - 1] == '.' || is_ascii_space(lowered_text[text_index - 1]))
        {
          score -= 20;
        }
        score += current;
        last_match = current;
        ++text_index;
        matched = true;
        break;
      }
      ++text_index;
    }
    if (!matched)
      return std::nullopt;
  }
  return score;
}

std::optional<int> fuzzy_text_score(std::string_view query, std::string_view text)
{
  auto const lowered_query = ascii_lower(query);
  auto const lowered_text = ascii_lower(text);
  auto const primary = fuzzy_ordered_text_score(lowered_query, lowered_text);
  if (primary)
    return primary;

  auto const swapped_query = swapped_alpha_numeric_query(lowered_query);
  if (!swapped_query)
    return std::nullopt;
  auto swapped_score = fuzzy_ordered_text_score(*swapped_query, lowered_text);
  if (!swapped_score)
    return std::nullopt;
  return *swapped_score + 5;
}

std::optional<int> file_reference_match_score(std::string_view query, std::string_view value)
{
  auto const tokens = file_reference_query_tokens(query);
  if (tokens.empty())
    return 0;
  int total = 0;
  for (auto const& token : tokens)
  {
    auto score = fuzzy_text_score(token, value);
    if (!score)
      return std::nullopt;
    total += *score;
  }
  return total;
}

bool file_reference_value_needs_quotes(std::string_view value)
{
  return value.find(' ') != std::string_view::npos;
}

std::string file_reference_token_text(FileReferenceItem const& reference, bool force_quote)
{
  auto const quoted = force_quote || file_reference_value_needs_quotes(reference.value);
  if (!quoted)
    return "@" + reference.value;
  return "@\"" + reference.value + "\"";
}

bool path_completion_value_is_natural(std::string_view value, bool quoted)
{
  if (quoted)
    return true;
  return value.starts_with(".") || value.find('/') != std::string_view::npos;
}

std::string path_completion_query_value(PathCompletionPrefix const& prefix)
{
  auto query = prefix.value;
  if (query.starts_with("./"))
    query.erase(0, 2);
  return query;
}

std::string path_completion_token_text(FileReferenceItem const& reference, PathCompletionPrefix const& prefix)
{
  auto value = reference.value;
  if (prefix.leading_dot_slash && !value.starts_with("./"))
    value = "./" + value;
  auto const quoted = prefix.quoted || file_reference_value_needs_quotes(value);
  if (!quoted)
    return value;
  return "\"" + value + "\"";
}

std::size_t current_argument_index(std::string_view text, std::vector<std::string> const& tokens)
{
  return ends_with_ascii_space(text) ? tokens.size() : tokens.empty() ? std::size_t{0} : tokens.size() - 1;
}

std::string current_argument_prefix(std::string_view text, std::vector<std::string> const& tokens)
{
  if (ends_with_ascii_space(text) || tokens.empty())
    return {};
  return tokens.back();
}

std::optional<int> slash_command_match_score(std::string_view command, std::string_view prefix)
{
  if (command.starts_with('/'))
    command.remove_prefix(1);
  return fuzzy_text_score(prefix, command);
}

std::optional<int> slash_command_item_match_score(SlashCommandItem const& item, std::string_view prefix)
{
  auto best = slash_command_match_score(item.command, prefix);
  for (auto const& alias : item.aliases)
  {
    auto const alias_score = slash_command_match_score(alias, prefix);
    if (alias_score && (!best || *alias_score < *best))
      best = alias_score;
  }
  return best;
}

bool slash_command_name_exact_match(std::string_view command, std::string_view prefix)
{
  if (command.starts_with('/'))
    command.remove_prefix(1);
  return command == prefix;
}

bool slash_command_exact_match(SlashCommandItem const& item, std::string_view prefix)
{
  if (slash_command_name_exact_match(item.command, prefix))
    return true;
  return std::ranges::any_of(item.aliases, [&](std::string_view alias) { return slash_command_name_exact_match(alias, prefix); });
}

bool slash_command_token_exact_match(SlashCommandItem const& item, std::string_view token)
{
  return item.command == token || std::ranges::find(item.aliases, token) != item.aliases.end();
}

SlashCommandItem const* find_slash_command_for_arguments(std::string_view input, std::vector<SlashCommandItem> const& commands)
{
  auto const token = command_token(input);
  if (token.empty())
    return nullptr;
  for (auto const& command : commands)
  {
    if (slash_command_token_exact_match(command, token))
      return &command;
  }
  return nullptr;
}

bool completion_previous_args_match(SlashCommandArgumentCompletion const& completion, std::vector<std::string> const& tokens)
{
  if (completion.required_previous_args.empty())
    return true;
  if (tokens.size() < completion.required_previous_args.size())
    return false;
  for (std::size_t index = 0; index < completion.required_previous_args.size(); ++index)
  {
    if (tokens[index] != completion.required_previous_args[index])
      return false;
  }
  return true;
}

std::optional<int> slash_argument_completion_match_score(SlashCommandArgumentCompletion const& completion,
                                                         std::string_view prefix)
{
  if (prefix.empty())
    return 0;
  if (completion.category == "Files")
  {
    if (!completion.value.starts_with(prefix))
      return std::nullopt;
    return -500 + static_cast<int>(prefix.size());
  }
  return fuzzy_text_score(prefix, completion.value + " " + completion.description + " " + completion.category);
}

std::string completion_insert_text(SlashCommandItem const& command, SlashCommandArgumentCompletion const& completion, std::vector<std::string> const& tokens,
                                   std::size_t argument_index)
{
  std::string text = command.command;
  std::vector<std::string> next_tokens;
  next_tokens.reserve(std::max(tokens.size(), argument_index + 1));
  for (std::size_t index = 0; index < argument_index && index < tokens.size(); ++index)
  {
    next_tokens.push_back(tokens[index]);
  }
  next_tokens.push_back(completion.value);
  for (auto const& token : next_tokens)
  {
    text += " " + token;
  }
  if (completion.append_space)
    text.push_back(' ');
  return text;
}

bool slash_command_has_argument_completions(std::string_view input, std::vector<SlashCommandItem> const& commands)
{
  auto const* command = find_slash_command_for_arguments(input, commands);
  if (!command || command->argument_completions.empty())
    return false;

  auto const args_text = argument_text(input);
  auto const tokens = split_argument_tokens(args_text);
  auto const argument_index = current_argument_index(args_text, tokens);
  auto const prefix = current_argument_prefix(args_text, tokens);
  return std::ranges::any_of(command->argument_completions, [&](SlashCommandArgumentCompletion const& completion) {
    return completion.argument_index == argument_index && completion_previous_args_match(completion, tokens) &&
           slash_argument_completion_match_score(completion, prefix).has_value();
  });
}

bool slash_argument_completion_exact_submission_ready(std::string_view input, std::vector<SlashCommandItem> const& commands)
{
  auto const* command = find_slash_command_for_arguments(input, commands);
  if (!command || !command->enabled || command->argument_completions.empty())
    return false;

  auto const args_text = argument_text(input);
  auto const tokens = split_argument_tokens(args_text);
  if (tokens.empty() || ends_with_ascii_space(args_text))
    return false;

  auto const argument_index = current_argument_index(args_text, tokens);
  auto const prefix = current_argument_prefix(args_text, tokens);
  for (auto const& completion : command->argument_completions)
  {
    if (completion.argument_index != argument_index || !completion.enabled)
      continue;
    if (!completion_previous_args_match(completion, tokens))
      continue;
    if (completion.value != prefix)
      continue;
    auto const has_deeper_same_argument_completion =
        std::ranges::any_of(command->argument_completions, [&](SlashCommandArgumentCompletion const& next) {
          return next.argument_index == argument_index && next.enabled && completion_previous_args_match(next, tokens) &&
                 next.value.size() > prefix.size() && next.value.starts_with(prefix);
        });
    if (has_deeper_same_argument_completion)
      return false;
    if (!completion.append_space)
      return true;

    auto accepted_tokens = tokens;
    accepted_tokens[argument_index] = completion.value;
    auto const next_argument_index = argument_index + 1;
    auto const has_next_completions = std::ranges::any_of(command->argument_completions, [&](SlashCommandArgumentCompletion const& next) {
      return next.argument_index == next_argument_index && completion_previous_args_match(next, accepted_tokens);
    });
    return !has_next_completions;
  }
  return false;
}

std::string slash_command_display(SlashCommandItem const& item)
{
  if (item.argument_completion)
    return item.command;
  auto text = item.command;
  if (!item.aliases.empty())
  {
    text += " (";
    for (std::size_t index = 0; index < item.aliases.size(); ++index)
    {
      if (index > 0)
        text += ", ";
      text += item.aliases[index];
    }
    text += ')';
  }
  return text;
}

std::string slash_command_hint_display(SlashCommandItem const& item)
{
  if (item.argument_completion)
    return item.hint;
  auto text = item.hint;
  if (!item.key_display.empty())
  {
    if (!text.empty())
      text += " · ";
    text += item.key_display;
  }
  return text;
}

std::string slash_command_description_display(SlashCommandItem const& item)
{
  auto text = item.description;
  if (!item.enabled)
  {
    if (!text.empty())
      text += " — ";
    text += "disabled";
    if (!item.disabled_reason.empty())
      text += ": " + item.disabled_reason;
  }
  return text;
}

std::vector<SlashCommandItem> filter_slash_argument_completions(std::string_view input, std::vector<SlashCommandItem> const& commands)
{
  auto const* command = find_slash_command_for_arguments(input, commands);
  if (!command || command->argument_completions.empty())
    return {};

  auto const args_text = argument_text(input);
  auto const tokens = split_argument_tokens(args_text);
  auto const argument_index = current_argument_index(args_text, tokens);
  auto const prefix = current_argument_prefix(args_text, tokens);
  std::vector<ScoredArgumentCompletion> scored;
  for (std::size_t index = 0; index < command->argument_completions.size(); ++index)
  {
    auto const& completion = command->argument_completions[index];
    if (completion.argument_index != argument_index)
      continue;
    if (!completion_previous_args_match(completion, tokens))
      continue;
    auto score = slash_argument_completion_match_score(completion, prefix);
    if (!score)
      continue;
    scored.push_back(ScoredArgumentCompletion{.completion = completion, .score = *score, .order = index});
  }

  if (!prefix.empty())
  {
    std::ranges::sort(scored, [](ScoredArgumentCompletion const& left, ScoredArgumentCompletion const& right) {
      if (left.score != right.score)
        return left.score < right.score;
      return left.order < right.order;
    });
  }

  std::vector<SlashCommandItem> matches;
  matches.reserve(scored.size());
  for (auto const& scored_completion : scored)
  {
    auto const& completion = scored_completion.completion;
    matches.push_back(SlashCommandItem{.command = completion.value,
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

std::optional<FileReferencePrefix> find_file_reference_prefix(std::string_view input, std::size_t cursor)
{
  if (input.empty() || input.starts_with('/'))
    return std::nullopt;
  cursor = cursor == std::string::npos ? input.size() : std::min(cursor, input.size());
  for (std::size_t index = cursor; index > 0; --index)
  {
    auto const at = index - 1;
    if (input[at] != '@' || at + 1 >= input.size() || input[at + 1] != '"' || !is_file_reference_boundary(input, at))
      continue;
    if (cursor < at + 2)
      continue;
    auto const quoted_value = input.substr(at + 2, cursor - at - 2);
    if (quoted_value.find('"') != std::string_view::npos)
      continue;
    return FileReferencePrefix{.start = at, .cursor = cursor, .value = std::string(quoted_value), .quoted = true};
  }

  auto start = cursor;
  while (start > 0 && !is_file_reference_boundary_char(input[start - 1])) --start;
  if (start >= input.size() || input[start] != '@')
    return std::nullopt;
  if (!is_file_reference_boundary(input, start))
    return std::nullopt;
  auto const value = input.substr(start + 1, cursor - start - 1);
  if (value.find('"') != std::string_view::npos)
    return std::nullopt;
  return FileReferencePrefix{.start = start, .cursor = cursor, .value = std::string(value), .quoted = false};
}

std::optional<PathCompletionPrefix> find_path_completion_prefix(std::string_view input, std::size_t cursor, bool force)
{
  if (input.empty())
  {
    if (!force)
      return std::nullopt;
    cursor = cursor == std::string::npos ? input.size() : std::min(cursor, input.size());
    return PathCompletionPrefix{.start = cursor, .cursor = cursor, .value = {}, .quoted = false, .leading_dot_slash = false};
  }
  cursor = cursor == std::string::npos ? input.size() : std::min(cursor, input.size());
  if (input.starts_with('/'))
  {
    auto const before_cursor = input.substr(0, cursor);
    auto const has_slash_argument = before_cursor.find_first_of(" \t\r\n") != std::string_view::npos;
    if (!force || !has_slash_argument)
      return std::nullopt;
  }
  for (std::size_t index = cursor; index > 0; --index)
  {
    auto const quote = index - 1;
    if (input[quote] != '"' || !is_file_reference_boundary(input, quote))
      continue;
    if (quote > 0 && input[quote - 1] == '@')
      continue;
    auto const quoted_value = input.substr(quote + 1, cursor - quote - 1);
    if (quoted_value.find('"') != std::string_view::npos)
      continue;
    if (!force && !path_completion_value_is_natural(quoted_value, true))
      return std::nullopt;
    return PathCompletionPrefix{.start = quote,
                                .cursor = cursor,
                                .value = std::string(quoted_value),
                                .quoted = true,
                                .leading_dot_slash = quoted_value.starts_with("./")};
  }

  auto start = cursor;
  while (start > 0 && !is_path_completion_delimiter(input[start - 1])) --start;
  if (start >= input.size())
  {
    if (!force)
      return std::nullopt;
    return PathCompletionPrefix{.start = cursor, .cursor = cursor, .value = {}, .quoted = false, .leading_dot_slash = false};
  }
  if (force && cursor > 0 && is_path_completion_delimiter(input[cursor - 1]))
  {
    return PathCompletionPrefix{.start = cursor, .cursor = cursor, .value = {}, .quoted = false, .leading_dot_slash = false};
  }
  auto const value = input.substr(start, cursor - start);
  if (value.empty() || input[start] == '@' || value.find('"') != std::string_view::npos)
    return std::nullopt;
  if (!force && !path_completion_value_is_natural(value, false))
    return std::nullopt;
  return PathCompletionPrefix{.start = start,
                              .cursor = cursor,
                              .value = std::string(value),
                              .quoted = false,
                              .leading_dot_slash = value.starts_with("./")};
}

std::vector<SlashCommandItem> file_reference_display_items(std::vector<FileReferenceItem> const& references, bool force_quote)
{
  std::vector<SlashCommandItem> items;
  items.reserve(references.size());
  for (auto const& reference : references)
  {
    items.push_back(SlashCommandItem{.command = file_reference_token_text(reference, force_quote),
                                     .description = reference.description,
                                     .hint = reference.directory ? "[directory]" : "",
                                     .category = reference.category,
                                     .enabled = reference.enabled,
                                     .disabled_reason = reference.disabled_reason,
                                     .argument_completion = true});
  }
  return items;
}

std::vector<SlashCommandItem> path_completion_display_items(std::vector<FileReferenceItem> const& references, PathCompletionPrefix const& prefix)
{
  std::vector<SlashCommandItem> items;
  items.reserve(references.size());
  for (auto const& reference : references)
  {
    items.push_back(SlashCommandItem{.command = path_completion_token_text(reference, prefix),
                                     .description = reference.description,
                                     .hint = reference.directory ? "[directory]" : "",
                                     .category = reference.category,
                                     .enabled = reference.enabled,
                                     .disabled_reason = reference.disabled_reason,
                                     .argument_completion = true});
  }
  return items;
}

std::string palette_prefix()
{
  return std::string(kSgrAccent) + std::string(kComposerBar) + std::string(kSgrReset) + std::string(kSgrComposerBg) + "  ";
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

std::string render_palette_item_columns(SlashCommandItem const& item, bool selected, std::size_t selected_index, std::size_t match_count, std::size_t width,
                                        std::size_t cmd_col_width, std::size_t category_col_width, std::size_t hint_col_width)
{
  static_cast<void>(selected_index);
  static_cast<void>(match_count);
  auto command_text = slash_command_display(item);
  auto const hint_text = slash_command_hint_display(item);
  auto const description_text = slash_command_description_display(item);

  std::string line = selected ? "› " : "  ";
  line += command_text;

  auto cmd_cols = detail::terminal_text_columns(command_text);
  if (cmd_cols < cmd_col_width && detail::terminal_text_columns(line) + (cmd_col_width - cmd_cols) <= width)
  {
    line += std::string(cmd_col_width - cmd_cols, ' ');
  }

  if (!item.category.empty())
  {
    line += "  " + item.category;
    auto category_cols = detail::terminal_text_columns(item.category);
    if (category_cols < category_col_width && detail::terminal_text_columns(line) + (category_col_width - category_cols) <= width)
    {
      line += std::string(category_col_width - category_cols, ' ');
    }
  }

  if (!hint_text.empty())
  {
    line += "  " + hint_text;
    auto hint_cols = detail::terminal_text_columns(hint_text);
    if (hint_cols < hint_col_width && detail::terminal_text_columns(line) + (hint_col_width - hint_cols) <= width)
    {
      line += std::string(hint_col_width - hint_cols, ' ');
    }
  }

  if (!description_text.empty())
  {
    line += "  " + description_text;
  }

  line = detail::fit_line(std::move(line), palette_content_width(width));
  if (selected)
  {
    line = std::string(kReverseVideo) + line + std::string(kSgrReset) + std::string(kSgrComposerBg);
  }
  if (!item.enabled && !selected)
    line = std::string(kSgrDim) + line + std::string(kSgrReset) + std::string(kSgrComposerBg);
  return palette_surface_line(std::move(line), width);
}

std::string render_palette_item_compact(SlashCommandItem const& item, bool selected, std::size_t selected_index, std::size_t match_count, std::size_t width)
{
  static_cast<void>(selected_index);
  static_cast<void>(match_count);
  std::string line = selected ? "› " : "  ";
  line += slash_command_display(item);
  auto const hint_text = slash_command_hint_display(item);
  if (!hint_text.empty())
  {
    line += " " + hint_text;
  }
  if (!item.category.empty())
  {
    line += "  [" + item.category + ']';
  }
  auto const description_text = slash_command_description_display(item);
  if (!description_text.empty())
  {
    line += "  " + description_text;
  }
  line = detail::fit_line(std::move(line), palette_content_width(width));
  if (selected)
  {
    line = std::string(kReverseVideo) + line + std::string(kSgrReset) + std::string(kSgrComposerBg);
  }
  if (!item.enabled && !selected)
    line = std::string(kSgrDim) + line + std::string(kSgrReset) + std::string(kSgrComposerBg);
  return palette_surface_line(std::move(line), width);
}

}  // namespace

std::vector<std::string> render_slash_palette(ComposerSnapshot const& snapshot, std::size_t width, std::size_t max_lines)
{
  std::vector<std::string> lines;
  if (max_lines == 0)
    return lines;
  auto const active_input = input_before_cursor(snapshot.input, snapshot.input_cursor);
  auto const matches = filter_slash_commands(snapshot.input, snapshot.input_cursor, snapshot.slash_commands);
  if (!slash_palette_visible(snapshot.input, snapshot.input_cursor, snapshot.slash_commands))
    return lines;

  auto const prefix = slash_command_prefix(active_input);
  auto const selected = clamp_slash_palette_selection(snapshot.input, snapshot.input_cursor, snapshot.slash_commands, snapshot.selected_slash_command_index);

  auto const item_budget = max_lines;

  if (matches.empty())
  {
    if (lines.size() < max_lines)
    {
      auto const text = has_argument_text(active_input) ? std::string("  no matching arguments")
                        : prefix.empty()                ? std::string("  no matching commands")
                                                        : "  no commands match /" + std::string(prefix);
      lines.push_back(palette_surface_line(text, width));
    }
    return lines;
  }

  auto const visible_items = std::min(matches.size(), item_budget);
  auto start = selected >= visible_items ? selected - visible_items + 1 : 0;
  if (start + visible_items > matches.size())
    start = matches.size() - visible_items;

  std::size_t max_cmd_cols = 0;
  std::size_t max_category_cols = 0;
  std::size_t max_hint_cols = 0;
  bool has_any_hint = false;
  bool has_any_category = false;
  for (std::size_t offset = 0; offset < visible_items; ++offset)
  {
    auto const& item = matches[start + offset];
    auto command_text = slash_command_display(item);
    max_cmd_cols = std::max(max_cmd_cols, detail::terminal_text_columns(command_text));
    if (!item.category.empty())
    {
      has_any_category = true;
      max_category_cols = std::max(max_category_cols, detail::terminal_text_columns(item.category));
    }
    auto const hint_text = slash_command_hint_display(item);
    if (!hint_text.empty())
    {
      has_any_hint = true;
      max_hint_cols = std::max(max_hint_cols, detail::terminal_text_columns(hint_text));
    }
  }

  bool const use_columns =
      width >= 40 && (2 + max_cmd_cols + (has_any_category ? max_category_cols + 2 : 0) + (has_any_hint ? max_hint_cols + 2 : 0) + 4 <= width);

  for (std::size_t offset = 0; offset < visible_items && lines.size() < max_lines; ++offset)
  {
    auto const index = start + offset;
    if (use_columns)
    {
      lines.push_back(
          render_palette_item_columns(matches[index], index == selected, selected, matches.size(), width, max_cmd_cols, max_category_cols, max_hint_cols));
    }
    else
    {
      lines.push_back(render_palette_item_compact(matches[index], index == selected, selected, matches.size(), width));
    }
  }

  return lines;
}

std::vector<std::string> render_file_reference_palette(ComposerSnapshot const& snapshot, std::size_t width, std::size_t max_lines)
{
  std::vector<std::string> lines;
  if (max_lines == 0 || !file_reference_palette_visible(snapshot.input, snapshot.input_cursor, snapshot.file_references))
    return lines;

  auto const matches = filter_file_references(snapshot.input, snapshot.input_cursor, snapshot.file_references);
  if (matches.empty())
    return lines;
  auto const prefix = find_file_reference_prefix(snapshot.input, snapshot.input_cursor);
  auto const selected =
      clamp_file_reference_selection(snapshot.input, snapshot.input_cursor, snapshot.file_references, snapshot.selected_slash_command_index);
  auto const display_items = file_reference_display_items(matches, prefix && prefix->quoted);
  auto const visible_items = std::min(display_items.size(), max_lines);
  auto start = selected >= visible_items ? selected - visible_items + 1 : 0;
  if (start + visible_items > display_items.size())
    start = display_items.size() - visible_items;

  std::size_t max_cmd_cols = 0;
  std::size_t max_category_cols = 0;
  std::size_t max_hint_cols = 0;
  bool has_any_hint = false;
  bool has_any_category = false;
  for (std::size_t offset = 0; offset < visible_items; ++offset)
  {
    auto const& item = display_items[start + offset];
    auto command_text = slash_command_display(item);
    max_cmd_cols = std::max(max_cmd_cols, detail::terminal_text_columns(command_text));
    if (!item.category.empty())
    {
      has_any_category = true;
      max_category_cols = std::max(max_category_cols, detail::terminal_text_columns(item.category));
    }
    auto const hint_text = slash_command_hint_display(item);
    if (!hint_text.empty())
    {
      has_any_hint = true;
      max_hint_cols = std::max(max_hint_cols, detail::terminal_text_columns(hint_text));
    }
  }

  bool const use_columns =
      width >= 40 && (2 + max_cmd_cols + (has_any_category ? max_category_cols + 2 : 0) + (has_any_hint ? max_hint_cols + 2 : 0) + 4 <= width);

  for (std::size_t offset = 0; offset < visible_items && lines.size() < max_lines; ++offset)
  {
    auto const index = start + offset;
    if (use_columns)
    {
      lines.push_back(render_palette_item_columns(display_items[index], index == selected, selected, display_items.size(), width, max_cmd_cols,
                                                  max_category_cols, max_hint_cols));
    }
    else
    {
      lines.push_back(render_palette_item_compact(display_items[index], index == selected, selected, display_items.size(), width));
    }
  }

  return lines;
}

std::vector<std::string> render_path_completion_palette(ComposerSnapshot const& snapshot, std::size_t width, std::size_t max_lines)
{
  std::vector<std::string> lines;
  if (max_lines == 0 || !path_completion_palette_visible(snapshot.input, snapshot.input_cursor, snapshot.file_references,
                                                         snapshot.path_completion_force_active))
    return lines;

  auto const matches =
      filter_path_completions(snapshot.input, snapshot.input_cursor, snapshot.file_references, snapshot.path_completion_force_active);
  if (matches.empty())
    return lines;
  auto const prefix = find_path_completion_prefix(snapshot.input, snapshot.input_cursor, snapshot.path_completion_force_active);
  if (!prefix)
    return lines;
  auto const selected =
      clamp_path_completion_selection(snapshot.input, snapshot.input_cursor, snapshot.file_references, snapshot.selected_slash_command_index,
                                      snapshot.path_completion_force_active);
  auto const display_items = path_completion_display_items(matches, *prefix);
  auto const visible_items = std::min(display_items.size(), max_lines);
  auto start = selected >= visible_items ? selected - visible_items + 1 : 0;
  if (start + visible_items > display_items.size())
    start = display_items.size() - visible_items;

  std::size_t max_cmd_cols = 0;
  std::size_t max_category_cols = 0;
  std::size_t max_hint_cols = 0;
  bool has_any_hint = false;
  bool has_any_category = false;
  for (std::size_t offset = 0; offset < visible_items; ++offset)
  {
    auto const& item = display_items[start + offset];
    auto command_text = slash_command_display(item);
    max_cmd_cols = std::max(max_cmd_cols, detail::terminal_text_columns(command_text));
    if (!item.category.empty())
    {
      has_any_category = true;
      max_category_cols = std::max(max_category_cols, detail::terminal_text_columns(item.category));
    }
    auto const hint_text = slash_command_hint_display(item);
    if (!hint_text.empty())
    {
      has_any_hint = true;
      max_hint_cols = std::max(max_hint_cols, detail::terminal_text_columns(hint_text));
    }
  }

  bool const use_columns =
      width >= 40 && (2 + max_cmd_cols + (has_any_category ? max_category_cols + 2 : 0) + (has_any_hint ? max_hint_cols + 2 : 0) + 4 <= width);

  for (std::size_t offset = 0; offset < visible_items && lines.size() < max_lines; ++offset)
  {
    auto const index = start + offset;
    if (use_columns)
    {
      lines.push_back(render_palette_item_columns(display_items[index], index == selected, selected, display_items.size(), width, max_cmd_cols,
                                                  max_category_cols, max_hint_cols));
    }
    else
    {
      lines.push_back(render_palette_item_compact(display_items[index], index == selected, selected, display_items.size(), width));
    }
  }

  return lines;
}

}  // namespace detail

namespace {

std::optional<std::size_t> palette_selection_for_screen_row(ComposerSnapshot const& snapshot, std::size_t row,
                                                            std::size_t item_count, std::size_t selected_index)
{
  if (row == 0 || snapshot.permission_prompt || snapshot.question_prompt || snapshot.select_list ||
      snapshot.slash_palette_suppressed || item_count == 0)
  {
    return std::nullopt;
  }

  auto const height = std::max<std::size_t>(detail::kMinHeight, snapshot.height);
  auto const fixed_lines = detail::composer_block_line_count(snapshot, height, composer_main_width(snapshot));
  auto const palette_line_budget =
      height > fixed_lines ? std::min(detail::kMaxPaletteLines, height - fixed_lines) : 0;
  if (palette_line_budget == 0)
    return std::nullopt;

  auto const visible_items = std::min(item_count, palette_line_budget);
  if (visible_items == 0)
    return std::nullopt;

  auto const selected = std::min(selected_index, item_count - 1);
  auto start = selected >= visible_items ? selected - visible_items + 1 : std::size_t{0};
  if (start + visible_items > item_count)
    start = item_count - visible_items;

  auto const non_transcript_lines = fixed_lines + visible_items;
  auto const transcript_height = height > non_transcript_lines ? height - non_transcript_lines : 0;
  auto const first_item_row = transcript_height + 1;
  if (row < first_item_row)
    return std::nullopt;
  auto const item_offset = row - first_item_row;
  if (item_offset >= visible_items)
    return std::nullopt;
  return start + item_offset;
}

}  // namespace

std::vector<SlashCommandItem> filter_slash_commands(std::string_view input, std::size_t cursor,
                                                    std::vector<SlashCommandItem> const& commands)
{
  input = detail::input_before_cursor(input, cursor);
  if (!input.starts_with('/'))
    return {};
  if (detail::has_argument_text(input))
    return detail::filter_slash_argument_completions(input, commands);

  std::vector<detail::ScoredSlashCommand> scored;
  auto const prefix = detail::slash_command_prefix(input);
  for (std::size_t index = 0; index < commands.size(); ++index)
  {
    auto const& command = commands[index];
    auto score = detail::slash_command_item_match_score(command, prefix);
    if (!score)
      continue;
    scored.push_back(detail::ScoredSlashCommand{.item = command, .score = *score, .order = index});
  }

  std::ranges::sort(scored, [](detail::ScoredSlashCommand const& left, detail::ScoredSlashCommand const& right) {
    if (left.score != right.score)
      return left.score < right.score;
    return left.order < right.order;
  });

  std::vector<SlashCommandItem> matches;
  matches.reserve(scored.size());
  for (auto const& match : scored)
  {
    matches.push_back(match.item);
  }
  return matches;
}

std::vector<SlashCommandItem> filter_slash_commands(std::string_view input, std::vector<SlashCommandItem> const& commands)
{
  return filter_slash_commands(input, input.size(), commands);
}

bool slash_palette_visible(std::string_view input, std::size_t cursor, std::vector<SlashCommandItem> const& commands)
{
  input = detail::input_before_cursor(input, cursor);
  if (!input.starts_with('/') || commands.empty())
    return false;
  if (detail::has_argument_text(input))
  {
    if (detail::slash_argument_completion_exact_submission_ready(input, commands))
      return false;
    return detail::slash_command_has_argument_completions(input, commands);
  }

  auto const prefix = detail::slash_command_prefix(input);
  for (auto const& command : commands)
  {
    if (detail::slash_command_exact_match(command, prefix) && command.hint.empty() && command.enabled)
      return false;
  }
  return true;
}

bool slash_palette_visible(std::string_view input, std::vector<SlashCommandItem> const& commands)
{
  return slash_palette_visible(input, input.size(), commands);
}

std::size_t clamp_slash_palette_selection(std::string_view input, std::size_t cursor,
                                          std::vector<SlashCommandItem> const& commands, std::size_t selected_index)
{
  auto const matches = filter_slash_commands(input, cursor, commands);
  if (matches.empty())
    return 0;
  return std::min(selected_index, matches.size() - 1);
}

std::size_t clamp_slash_palette_selection(std::string_view input, std::vector<SlashCommandItem> const& commands, std::size_t selected_index)
{
  return clamp_slash_palette_selection(input, input.size(), commands, selected_index);
}

std::size_t previous_slash_palette_selection(std::string_view input, std::size_t cursor,
                                             std::vector<SlashCommandItem> const& commands, std::size_t selected_index)
{
  auto const matches = filter_slash_commands(input, cursor, commands);
  if (matches.empty())
    return 0;
  auto const selected = std::min(selected_index, matches.size() - 1);
  return selected == 0 ? matches.size() - 1 : selected - 1;
}

std::size_t previous_slash_palette_selection(std::string_view input, std::vector<SlashCommandItem> const& commands, std::size_t selected_index)
{
  return previous_slash_palette_selection(input, input.size(), commands, selected_index);
}

std::size_t next_slash_palette_selection(std::string_view input, std::size_t cursor,
                                         std::vector<SlashCommandItem> const& commands, std::size_t selected_index)
{
  auto const matches = filter_slash_commands(input, cursor, commands);
  if (matches.empty())
    return 0;
  auto const selected = std::min(selected_index, matches.size() - 1);
  return (selected + 1) % matches.size();
}

std::size_t next_slash_palette_selection(std::string_view input, std::vector<SlashCommandItem> const& commands, std::size_t selected_index)
{
  return next_slash_palette_selection(input, input.size(), commands, selected_index);
}

SlashCommandSelectionText slash_command_selection_text(std::string_view input, std::size_t cursor,
                                                       std::vector<SlashCommandItem> const& commands,
                                                       std::size_t selected_index)
{
  auto const effective = detail::effective_cursor(input, cursor);
  auto const matches = filter_slash_commands(input, effective, commands);
  if (matches.empty())
    return SlashCommandSelectionText{.text = std::string(input), .cursor = effective};
  auto const selected = std::min(selected_index, matches.size() - 1);
  std::string replacement;
  if (matches[selected].argument_completion)
  {
    replacement = matches[selected].completion_insert_text;
  }
  else
  {
    replacement = matches[selected].command;
    if (!matches[selected].hint.empty())
      replacement.push_back(' ');
  }

  auto suffix = input.substr(effective);
  if (!replacement.empty() && replacement.back() == ' ' && !suffix.empty() && detail::is_ascii_space(suffix.front()))
    suffix.remove_prefix(1);

  auto text = replacement;
  auto const next_cursor = text.size();
  text += suffix;
  return SlashCommandSelectionText{.text = std::move(text), .cursor = next_cursor};
}

std::string slash_command_selection_text(std::string_view input, std::vector<SlashCommandItem> const& commands, std::size_t selected_index)
{
  return slash_command_selection_text(input, input.size(), commands, selected_index).text;
}

std::optional<std::string> slash_command_selection_disabled_reason(std::string_view input, std::size_t cursor,
                                                                   std::vector<SlashCommandItem> const& commands,
                                                                   std::size_t selected_index)
{
  auto const matches = filter_slash_commands(input, cursor, commands);
  if (matches.empty())
    return std::nullopt;
  auto const selected = std::min(selected_index, matches.size() - 1);
  if (matches[selected].enabled)
    return std::nullopt;
  if (!matches[selected].disabled_reason.empty())
    return matches[selected].disabled_reason;
  return std::string("command is disabled");
}

std::optional<std::string> slash_command_selection_disabled_reason(std::string_view input, std::vector<SlashCommandItem> const& commands,
                                                                   std::size_t selected_index)
{
  return slash_command_selection_disabled_reason(input, input.size(), commands, selected_index);
}

std::vector<FileReferenceItem> filter_file_references(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references)
{
  std::vector<detail::ScoredFileReference> scored;
  auto const prefix = detail::find_file_reference_prefix(input, cursor);
  if (!prefix)
    return {};
  for (auto const& reference : references)
  {
    auto score = detail::file_reference_match_score(prefix->value, reference.value);
    if (!score)
      continue;
    scored.push_back(detail::ScoredFileReference{.item = reference, .score = *score});
  }
  std::ranges::sort(scored, [](detail::ScoredFileReference const& left, detail::ScoredFileReference const& right) {
    if (left.item.directory != right.item.directory)
      return left.item.directory > right.item.directory;
    if (left.score != right.score)
      return left.score < right.score;
    return detail::ascii_lower(left.item.value) < detail::ascii_lower(right.item.value);
  });

  std::vector<FileReferenceItem> matches;
  matches.reserve(scored.size());
  for (auto const& match : scored)
  {
    matches.push_back(match.item);
  }
  return matches;
}

bool file_reference_palette_visible(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references)
{
  if (references.empty())
    return false;
  auto const prefix = detail::find_file_reference_prefix(input, cursor);
  if (!prefix)
    return false;
  auto const matches = filter_file_references(input, cursor, references);
  if (matches.empty())
    return false;
  if (matches.size() == 1 && matches.front().enabled && !matches.front().directory && matches.front().value == prefix->value)
    return false;
  return true;
}

std::size_t clamp_file_reference_selection(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                           std::size_t selected_index)
{
  auto const matches = filter_file_references(input, cursor, references);
  if (matches.empty())
    return 0;
  return std::min(selected_index, matches.size() - 1);
}

std::size_t previous_file_reference_selection(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                              std::size_t selected_index)
{
  auto const matches = filter_file_references(input, cursor, references);
  if (matches.empty())
    return 0;
  auto const selected = std::min(selected_index, matches.size() - 1);
  return selected == 0 ? matches.size() - 1 : selected - 1;
}

std::size_t next_file_reference_selection(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                          std::size_t selected_index)
{
  auto const matches = filter_file_references(input, cursor, references);
  if (matches.empty())
    return 0;
  auto const selected = std::min(selected_index, matches.size() - 1);
  return (selected + 1) % matches.size();
}

FileReferenceSelectionText file_reference_selection_text(std::string_view input, std::size_t cursor,
                                                         std::vector<FileReferenceItem> const& references,
                                                         std::size_t selected_index)
{
  auto const prefix = detail::find_file_reference_prefix(input, cursor);
  auto const matches = filter_file_references(input, cursor, references);
  if (!prefix || matches.empty())
    return FileReferenceSelectionText{.text = std::string(input),
                                      .cursor = cursor == std::string::npos ? input.size() : std::min(cursor, input.size())};
  auto const selected = std::min(selected_index, matches.size() - 1);
  auto replacement = detail::file_reference_token_text(matches[selected], prefix->quoted);
  auto cursor_after_replacement = prefix->start + replacement.size();
  if (matches[selected].directory && replacement.ends_with('"'))
    --cursor_after_replacement;
  if (!matches[selected].directory &&
      (prefix->cursor >= input.size() ||
       (!detail::is_ascii_space(input[prefix->cursor]) && !detail::is_file_reference_closing_boundary(input[prefix->cursor]))))
  {
    replacement.push_back(' ');
    cursor_after_replacement = prefix->start + replacement.size();
  }
  auto replacement_end = prefix->cursor;
  if (prefix->quoted && replacement_end < input.size() && input[replacement_end] == '"' && replacement.find('"') != std::string::npos)
    ++replacement_end;
  auto text = std::string(input.substr(0, prefix->start));
  text += replacement;
  text += input.substr(replacement_end);
  return FileReferenceSelectionText{.text = std::move(text), .cursor = cursor_after_replacement};
}

std::optional<std::size_t> file_reference_palette_selection_for_screen_row(ComposerSnapshot const& snapshot,
                                                                           std::size_t row)
{
  if (slash_palette_visible(snapshot.input, snapshot.input_cursor, snapshot.slash_commands) ||
      !file_reference_palette_visible(snapshot.input, snapshot.input_cursor, snapshot.file_references))
  {
    return std::nullopt;
  }
  auto const matches = filter_file_references(snapshot.input, snapshot.input_cursor, snapshot.file_references);
  auto const selected =
      clamp_file_reference_selection(snapshot.input, snapshot.input_cursor, snapshot.file_references,
                                     snapshot.selected_slash_command_index);
  return palette_selection_for_screen_row(snapshot, row, matches.size(), selected);
}

std::vector<FileReferenceItem> filter_path_completions(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                                       bool force)
{
  std::vector<detail::ScoredFileReference> scored;
  auto const prefix = detail::find_path_completion_prefix(input, cursor, force);
  if (!prefix)
    return {};
  auto const query = detail::path_completion_query_value(*prefix);
  for (auto const& reference : references)
  {
    auto score = detail::file_reference_match_score(query, reference.value);
    if (!score)
      continue;
    scored.push_back(detail::ScoredFileReference{.item = reference, .score = *score});
  }
  std::ranges::sort(scored, [](detail::ScoredFileReference const& left, detail::ScoredFileReference const& right) {
    if (left.item.directory != right.item.directory)
      return left.item.directory > right.item.directory;
    if (left.score != right.score)
      return left.score < right.score;
    return detail::ascii_lower(left.item.value) < detail::ascii_lower(right.item.value);
  });

  std::vector<FileReferenceItem> matches;
  matches.reserve(scored.size());
  for (auto const& match : scored)
  {
    matches.push_back(match.item);
  }
  return matches;
}

bool path_completion_palette_visible(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                     bool force)
{
  if (references.empty())
    return false;
  auto const prefix = detail::find_path_completion_prefix(input, cursor, force);
  if (!prefix)
    return false;
  auto const matches = filter_path_completions(input, cursor, references, force);
  if (matches.empty())
    return false;
  auto const query = detail::path_completion_query_value(*prefix);
  if (matches.size() == 1 && matches.front().enabled && !matches.front().directory && matches.front().value == query)
    return false;
  return true;
}

std::size_t clamp_path_completion_selection(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                            std::size_t selected_index, bool force)
{
  auto const matches = filter_path_completions(input, cursor, references, force);
  if (matches.empty())
    return 0;
  return std::min(selected_index, matches.size() - 1);
}

std::size_t previous_path_completion_selection(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                               std::size_t selected_index, bool force)
{
  auto const matches = filter_path_completions(input, cursor, references, force);
  if (matches.empty())
    return 0;
  auto const selected = std::min(selected_index, matches.size() - 1);
  return selected == 0 ? matches.size() - 1 : selected - 1;
}

std::size_t next_path_completion_selection(std::string_view input, std::size_t cursor, std::vector<FileReferenceItem> const& references,
                                           std::size_t selected_index, bool force)
{
  auto const matches = filter_path_completions(input, cursor, references, force);
  if (matches.empty())
    return 0;
  auto const selected = std::min(selected_index, matches.size() - 1);
  return (selected + 1) % matches.size();
}

PathCompletionSelectionText path_completion_selection_text(std::string_view input, std::size_t cursor,
                                                           std::vector<FileReferenceItem> const& references,
                                                           std::size_t selected_index,
                                                           bool force)
{
  auto const prefix = detail::find_path_completion_prefix(input, cursor, force);
  auto const matches = filter_path_completions(input, cursor, references, force);
  if (!prefix || matches.empty())
    return PathCompletionSelectionText{.text = std::string(input),
                                       .cursor = cursor == std::string::npos ? input.size() : std::min(cursor, input.size())};
  auto const selected = std::min(selected_index, matches.size() - 1);
  auto replacement = detail::path_completion_token_text(matches[selected], *prefix);
  auto cursor_after_replacement = prefix->start + replacement.size();
  if (matches[selected].directory && replacement.ends_with('"'))
    --cursor_after_replacement;
  auto replacement_end = prefix->cursor;
  if (prefix->quoted && replacement_end < input.size() && input[replacement_end] == '"' && replacement.ends_with('"'))
    ++replacement_end;
  auto text = std::string(input.substr(0, prefix->start));
  text += replacement;
  text += input.substr(replacement_end);
  return PathCompletionSelectionText{.text = std::move(text), .cursor = cursor_after_replacement};
}

std::optional<std::size_t> path_completion_palette_selection_for_screen_row(ComposerSnapshot const& snapshot,
                                                                            std::size_t row)
{
  if (slash_palette_visible(snapshot.input, snapshot.input_cursor, snapshot.slash_commands) ||
      file_reference_palette_visible(snapshot.input, snapshot.input_cursor, snapshot.file_references) ||
      !path_completion_palette_visible(snapshot.input, snapshot.input_cursor, snapshot.file_references,
                                       snapshot.path_completion_force_active))
  {
    return std::nullopt;
  }
  auto const matches = filter_path_completions(snapshot.input, snapshot.input_cursor, snapshot.file_references,
                                              snapshot.path_completion_force_active);
  auto const selected = clamp_path_completion_selection(snapshot.input, snapshot.input_cursor,
                                                       snapshot.file_references,
                                                       snapshot.selected_slash_command_index,
                                                       snapshot.path_completion_force_active);
  return palette_selection_for_screen_row(snapshot, row, matches.size(), selected);
}

std::optional<std::size_t> slash_palette_selection_for_screen_row(ComposerSnapshot const& snapshot, std::size_t row)
{
  if (row == 0 || snapshot.permission_prompt || snapshot.question_prompt || snapshot.slash_palette_suppressed ||
      !slash_palette_visible(snapshot.input, snapshot.input_cursor, snapshot.slash_commands))
  {
    return std::nullopt;
  }
  auto const height = std::max<std::size_t>(detail::kMinHeight, snapshot.height);
  auto const matches = filter_slash_commands(snapshot.input, snapshot.input_cursor, snapshot.slash_commands);
  if (matches.empty())
    return std::nullopt;

  auto const fixed_lines = detail::composer_block_line_count(snapshot, height, composer_main_width(snapshot));
  auto const palette_line_budget = height > fixed_lines ? std::min(detail::kMaxPaletteLines, height - fixed_lines) : 0;
  if (palette_line_budget == 0)
    return std::nullopt;

  auto const item_budget = palette_line_budget;
  auto const visible_items = std::min(matches.size(), item_budget);
  if (visible_items == 0)
    return std::nullopt;

  auto const selected = clamp_slash_palette_selection(snapshot.input, snapshot.input_cursor, snapshot.slash_commands, snapshot.selected_slash_command_index);
  auto start = selected >= visible_items ? selected - visible_items + 1 : 0;
  if (start + visible_items > matches.size())
    start = matches.size() - visible_items;

  auto const palette_lines = visible_items;
  auto const non_transcript_lines = fixed_lines + palette_lines;
  auto const transcript_height = height > non_transcript_lines ? height - non_transcript_lines : 0;
  auto const first_item_row = transcript_height + 1;
  if (row < first_item_row)
    return std::nullopt;
  auto const item_offset = row - first_item_row;
  if (item_offset >= visible_items)
    return std::nullopt;
  return start + item_offset;
}

}  // namespace ava::tui
