#include "sys.h"
#include "ava/tui/composer_internal.h"
#include "ava/permissions/permission.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>

namespace ava::tui {
namespace detail {
namespace {

constexpr std::string_view kCopyOptionPrefix = "copy:";

std::string render_permission_choice(std::string_view label, bool selected)
{
  std::string text = selected ? "› " : "  ";
  text += label;
  if (selected)
    return std::string(kSgrWarning) + std::string(kSgrBold) + text + std::string(kSgrReset);
  return std::string(kSgrMuted) + text + std::string(kSgrReset);
}

std::string permission_dock_header(std::size_t width)
{
  auto const label = width < 8 ? std::string("! PERM") : std::string("! Permission required");
  return fit_line_preserving_sgr("  " + std::string(kSgrWarning) + std::string(kSgrBold) + label + std::string(kSgrReset), width);
}

std::string permission_label_source(PermissionPromptView const& prompt)
{
  auto source = !prompt.tool_name.empty() ? prompt.tool_name : prompt.operation;
  source = sanitize_terminal_text(source);
  std::string lowered;
  lowered.reserve(source.size());
  for (char const ch : source) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  if (!prompt.command.empty() || lowered == "bash" || lowered == "shell" || lowered == "shell.run" || lowered == "run_command" || lowered == "run command")
  {
    return "Shell command";
  }
  if (source.empty())
    return "Requested action";
  for (char& ch : source)
  {
    if (ch == '_' || ch == '.')
      ch = ' ';
  }
  source.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(source.front())));
  return source;
}

std::string permission_subject(PermissionPromptView const& prompt)
{
  if (!prompt.command.empty())
    return "$ " + sanitize_terminal_text(prompt.command);
  if (!prompt.target.empty())
  {
    if (prompt.reason.find("outside the workspace") != std::string::npos)
      return "Access external directory " + sanitize_terminal_text(prompt.target);
    return sanitize_terminal_text(prompt.target);
  }
  if (!prompt.operation.empty() && sanitize_terminal_text(prompt.operation) != sanitize_terminal_text(prompt.tool_name))
    return sanitize_terminal_text(prompt.operation);
  return {};
}

std::string permission_dock_action(PermissionPromptView const& prompt, std::size_t width)
{
  return fit_line_preserving_sgr("  " + std::string(kSgrBold) + permission_label_source(prompt) + std::string(kSgrReset), width);
}

std::string permission_dock_subject(PermissionPromptView const& prompt, std::size_t width)
{
  return fit_line_preserving_sgr("  " + std::string(kSgrText) + permission_subject(prompt) + std::string(kSgrReset), width);
}

std::string permission_dock_compact_summary(PermissionPromptView const& prompt, std::size_t width)
{
  auto text = "  " + permission_label_source(prompt);
  if (auto const subject = permission_subject(prompt); !subject.empty())
    text += " · " + subject;
  return fit_line_preserving_sgr(std::string(kSgrText) + std::move(text) + std::string(kSgrReset), width);
}

std::string permission_metadata_text(PermissionPromptView const& prompt)
{
  std::vector<std::string> parts;
  if (!prompt.risk.empty())
    parts.push_back("risk " + sanitize_terminal_text(prompt.risk));
  if (!prompt.reason.empty())
    parts.push_back("reason " + sanitize_terminal_text(prompt.reason));
  if (parts.empty())
    return {};

  std::string text;
  for (std::size_t index = 0; index < parts.size(); ++index)
  {
    if (index > 0)
      text += "  ";
    text += parts[index];
  }
  return text;
}

std::string permission_dock_metadata(std::string_view metadata_text, std::size_t width)
{
  if (metadata_text.empty())
    return {};

  auto text = "  " + std::string(metadata_text);
  return fit_line_preserving_sgr(std::string(kSgrWarning) + text + std::string(kSgrReset), width);
}

std::string key_pill(std::string_view key)
{
  return std::string(kSgrBold) + std::string(key) + std::string(kSgrReset);
}

bool permission_choice_is_allow(PermissionPromptChoice choice)
{
  return choice == PermissionPromptChoice::Allow || choice == PermissionPromptChoice::AllowSession || choice == PermissionPromptChoice::AllowRemember;
}

bool permission_choice_is_remember(PermissionPromptChoice choice)
{
  return choice == PermissionPromptChoice::DenyRemember || choice == PermissionPromptChoice::AllowRemember;
}

bool permission_choice_can_be_remembered(PermissionPromptChoice choice, bool allow_remember_available, bool deny_remember_available)
{
  return permission_choice_is_allow(choice) ? allow_remember_available : deny_remember_available;
}

std::vector<PermissionPromptChoice> permission_choices(bool allow_session_available, bool allow_remember_available, bool deny_remember_available)
{
  std::vector<PermissionPromptChoice> choices{PermissionPromptChoice::Deny, PermissionPromptChoice::Allow};
  if (allow_session_available)
    choices.push_back(PermissionPromptChoice::AllowSession);
  if (deny_remember_available)
    choices.push_back(PermissionPromptChoice::DenyRemember);
  if (allow_remember_available)
    choices.push_back(PermissionPromptChoice::AllowRemember);
  return choices;
}

PermissionPromptChoice next_permission_choice(PermissionPromptChoice selected, bool allow_session_available, bool allow_remember_available,
                                              bool deny_remember_available)
{
  auto choices = permission_choices(allow_session_available, allow_remember_available, deny_remember_available);
  auto const found = std::ranges::find(choices, selected);
  if (found == choices.end())
    return choices.front();
  auto const index = static_cast<std::size_t>(std::distance(choices.begin(), found));
  return choices[(index + 1) % choices.size()];
}

PermissionPromptChoice previous_permission_choice(PermissionPromptChoice selected, bool allow_session_available, bool allow_remember_available,
                                                  bool deny_remember_available)
{
  auto choices = permission_choices(allow_session_available, allow_remember_available, deny_remember_available);
  auto const found = std::ranges::find(choices, selected);
  if (found == choices.end())
    return choices.front();
  auto const index = static_cast<std::size_t>(std::distance(choices.begin(), found));
  return choices[index == 0 ? choices.size() - 1 : index - 1];
}

PermissionPromptChoice remembered_permission_choice(PermissionPromptChoice selected)
{
  return permission_choice_is_allow(selected) ? PermissionPromptChoice::AllowRemember : PermissionPromptChoice::DenyRemember;
}

PermissionPromptChoice one_shot_permission_choice(PermissionPromptChoice selected)
{
  return permission_choice_is_allow(selected) ? PermissionPromptChoice::Allow : PermissionPromptChoice::Deny;
}

PermissionPromptInputAction resolve_permission_choice_action(PermissionPromptChoice selected)
{
  switch (selected)
  {
    case PermissionPromptChoice::Allow:
      return PermissionPromptInputAction::ResolveAllow;
    case PermissionPromptChoice::AllowSession:
      return PermissionPromptInputAction::ResolveAllowSession;
    case PermissionPromptChoice::Deny:
      return PermissionPromptInputAction::ResolveDeny;
    case PermissionPromptChoice::AllowRemember:
      return PermissionPromptInputAction::ResolveAllowRemember;
    case PermissionPromptChoice::DenyRemember:
      return PermissionPromptInputAction::ResolveDenyRemember;
  }
  return PermissionPromptInputAction::ResolveDeny;
}

std::string permission_dock_actions(PermissionPromptChoice selected, bool allow_session_available, bool allow_remember_available, bool deny_remember_available,
                                    std::size_t width)
{
  auto compose = [&](std::array<std::string_view, 5> const& labels) {
    std::string text = "  ";
    auto append = [&](std::string_view label, PermissionPromptChoice choice) {
      if (terminal_text_columns(text) > 2)
        text += " ";
      text += render_permission_choice(label, selected == choice);
    };
    append(labels[0], PermissionPromptChoice::Deny);
    append(labels[1], PermissionPromptChoice::Allow);
    if (allow_session_available)
      append(labels[2], PermissionPromptChoice::AllowSession);
    if (deny_remember_available)
      append(labels[3], PermissionPromptChoice::DenyRemember);
    if (allow_remember_available)
      append(labels[4], PermissionPromptChoice::AllowRemember);
    return text;
  };

  std::array const full = {std::string_view("Reject"), std::string_view("Allow once"), std::string_view("Allow session"), std::string_view("Always reject"),
                           std::string_view("Always allow")};
  std::array const compact = {std::string_view("Reject"), std::string_view("Once"), std::string_view("Session"), std::string_view("Never"),
                              std::string_view("Always")};
  std::array const narrow = {std::string_view("No"), std::string_view("Yes"), std::string_view("Sess"), std::string_view("Never"), std::string_view("Always")};
  std::array const candidates = {compose(full), compose(compact), compose(narrow)};
  for (auto const& candidate : candidates)
  {
    if (terminal_text_columns(candidate) <= width)
      return candidate;
  }
  return fit_line_preserving_sgr(candidates.back(), width);
}

std::string permission_dock_keys(bool allow_session_available, bool allow_remember_available, bool deny_remember_available, std::size_t width)
{
  auto shortcuts = std::string("A/D shortcuts");
  if (allow_session_available)
    shortcuts = "A/S/D shortcuts";
  if (allow_remember_available || deny_remember_available)
    shortcuts += "  R remember";
  std::array const candidates = {
      std::string("  ") + key_pill("↑/↓") + " move  " + key_pill("Enter") + " confirm  " + shortcuts + "  " + key_pill("Esc") + " reject",
      std::string("  ") + key_pill("Enter") + " confirm  " + shortcuts + "  " + key_pill("Esc") + " reject",
      std::string("  ") + key_pill("Enter") + " confirm  " + key_pill("Esc") + " reject",
      std::string("  ") + key_pill("A") + " allow  " + key_pill("D") + " reject",
  };
  for (auto const& candidate : candidates)
  {
    if (terminal_text_columns(candidate) <= width)
      return std::string(kSgrMuted) + candidate + std::string(kSgrReset);
  }
  return fit_line_preserving_sgr(std::string(kSgrMuted) + candidates.back() + std::string(kSgrReset), width);
}

void append_permission_diff_lines(std::vector<std::string>& lines, PermissionPromptView const& prompt, std::size_t width, std::size_t budget)
{
  if (prompt.diff_preview.empty() || budget == 0)
    return;
  auto const prefix = std::string("  ");
  lines.push_back(fit_line_preserving_sgr(prefix + std::string(kSgrDim) + "diff:" + std::string(kSgrReset), width));
  if (lines.size() >= budget)
    return;

  auto const line_prefix = std::string("    ");
  auto diff_lines =
      render_unified_diff_body(prompt.diff_preview, prompt.diff_truncated, width, line_prefix, budget > lines.size() ? budget - lines.size() : std::size_t{0});
  lines.insert(lines.end(), diff_lines.begin(), diff_lines.end());
}

std::string question_title(QuestionPromptView const& prompt)
{
  auto const title = prompt.header.empty() ? std::string("Question") : sanitize_terminal_text(prompt.header);
  return "? " + title;
}

std::string question_dock_header(QuestionPromptView const& prompt, std::size_t width)
{
  return fit_line_preserving_sgr("  " + std::string(kSgrAccent) + std::string(kSgrBold) + question_title(prompt) + std::string(kSgrReset), width);
}

bool question_custom_owns_cursor(QuestionPromptView const& prompt)
{
  return !prompt.multiple && prompt.allow_custom && !prompt.custom_text.empty();
}

std::string question_option_line(QuestionPromptView const& prompt, std::size_t index, std::size_t width)
{
  auto const& option = prompt.options[index];
  auto const cursor = index == prompt.selected_option_index && !question_custom_owns_cursor(prompt);
  std::string text = cursor ? "  › " : "    ";
  text += std::to_string(index + 1) + ". ";
  if (prompt.multiple)
    text += option.selected ? "✓ " : "· ";
  text += sanitize_terminal_text(option.label.empty() ? option.value : option.label);
  auto const style = cursor ? std::string(kSgrAccent) + std::string(kSgrBold) : std::string(kSgrMuted);
  return fit_line_preserving_sgr(style + text + std::string(kSgrReset), width);
}

std::string question_custom_line(QuestionPromptView const& prompt, std::size_t width)
{
  auto const cursor = question_custom_owns_cursor(prompt);
  std::string text = cursor ? "  › Custom: " : "    Custom: ";
  if (prompt.custom_text.empty())
  {
    text += prompt.secret ? std::string("paste secret") : std::string("type to answer");
  }
  else if (prompt.secret)
  {
    text += std::string(std::min<std::size_t>(prompt.custom_text.size(), 64), '*');
  }
  else
  {
    text += sanitize_terminal_text(prompt.custom_text);
  }
  auto const style = cursor ? std::string(kSgrAccent) + std::string(kSgrBold) : std::string(kSgrTextDimmed);
  return fit_line_preserving_sgr(style + text + std::string(kSgrReset), width);
}

std::string lower_ascii(std::string_view text)
{
  std::string lowered;
  lowered.reserve(text.size());
  for (char const ch : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

bool prompt_option_matches(QuestionPromptView const& prompt, std::size_t index)
{
  if (!prompt.searchable || prompt.custom_text.empty())
    return true;
  auto const query = lower_ascii(prompt.custom_text);
  auto const& option = prompt.options[index];
  auto const label = lower_ascii(option.label.empty() ? option.value : option.label);
  auto const value = lower_ascii(option.value);
  return label.find(query) != std::string::npos || value.find(query) != std::string::npos;
}

std::vector<std::size_t> matching_option_indices(QuestionPromptView const& prompt)
{
  std::vector<std::size_t> indices;
  for (std::size_t index = 0; index < prompt.options.size(); ++index)
  {
    if (prompt_option_matches(prompt, index))
      indices.push_back(index);
  }
  return indices;
}

std::size_t visible_option_window_start(std::vector<std::size_t> const& indices, std::size_t selected, std::size_t row_budget)
{
  if (indices.empty() || row_budget == 0)
    return 0;
  auto const selected_it = std::ranges::find(indices, selected);
  auto const selected_visible = selected_it == indices.end() ? std::size_t{0} : static_cast<std::size_t>(selected_it - indices.begin());
  return selected_visible >= row_budget ? selected_visible - row_budget + 1 : std::size_t{0};
}

struct QuestionModalContentRow
{
  std::optional<std::size_t> option_index;
  std::string section;
};

std::vector<QuestionModalContentRow> visible_question_modal_rows(QuestionPromptView const& prompt, std::size_t row_budget)
{
  std::vector<QuestionModalContentRow> all_rows;
  auto const matches = matching_option_indices(prompt);
  if (matches.empty() || row_budget == 0)
    return all_rows;

  if (prompt.searchable)
    all_rows.push_back(QuestionModalContentRow{.option_index = std::nullopt, .section = "Popular"});
  for (std::size_t visible = 0; visible < matches.size(); ++visible)
  {
    if (prompt.searchable && visible == 4)
      all_rows.push_back(QuestionModalContentRow{.option_index = std::nullopt, .section = "Other"});
    all_rows.push_back(QuestionModalContentRow{.option_index = matches[visible], .section = {}});
  }

  auto const selected = std::min(prompt.selected_option_index, prompt.options.size() - 1);
  auto const selected_it = std::ranges::find_if(all_rows, [selected](QuestionModalContentRow const& row) { return row.option_index == selected; });
  auto const selected_row = selected_it == all_rows.end() ? std::size_t{0} : static_cast<std::size_t>(selected_it - all_rows.begin());
  auto start = selected_row >= row_budget ? selected_row - row_budget + 1 : std::size_t{0};
  if (start > 0 && all_rows[start].option_index && !all_rows[start - 1].option_index && selected_row - (start - 1) < row_budget)
  {
    --start;
  }
  auto const end = std::min(all_rows.size(), start + row_budget);
  return std::vector<QuestionModalContentRow>(all_rows.begin() + static_cast<std::ptrdiff_t>(start), all_rows.begin() + static_cast<std::ptrdiff_t>(end));
}

std::optional<std::size_t> option_index_for_shortcut(QuestionPromptInputResult const& result, char character)
{
  auto const wanted = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  if (wanted == '\0')
    return std::nullopt;
  for (std::size_t index = 0; index < result.options.size(); ++index)
  {
    auto const text = result.options[index].label.empty() ? result.options[index].value : result.options[index].label;
    for (char const ch : text)
    {
      auto const byte = static_cast<unsigned char>(ch);
      if (std::isalnum(byte) == 0)
        continue;
      if (static_cast<char>(std::tolower(byte)) == wanted)
        return index;
      break;
    }
  }
  return std::nullopt;
}

std::optional<std::string_view> copy_text_for_option(QuestionPromptOptionView const& option)
{
  if (!option.value.starts_with(kCopyOptionPrefix))
    return std::nullopt;
  return std::string_view(option.value).substr(kCopyOptionPrefix.size());
}

std::string modal_line(std::string content, std::size_t width)
{
  return question_surface_line("  " + std::move(content), width);
}

std::vector<std::string> modal_wrapped_lines(std::string_view text, std::size_t width)
{
  std::vector<std::string> lines;
  auto const content_width = width > 4 ? width - 4 : width;
  for (auto const& raw_line : split_lines(text))
  {
    for (auto const& wrapped : wrap_transcript_text(raw_line, content_width))
    {
      lines.push_back(modal_line(wrapped, width));
    }
  }
  if (lines.empty())
    lines.push_back(modal_line("", width));
  return lines;
}

std::string modal_title_line(QuestionPromptView const& prompt, std::size_t width)
{
  std::string line = std::string(kSgrAccent) + std::string(kSgrBold) + question_title(prompt) + std::string(kSgrReset) + std::string(kSgrQuestionBg);
  std::string const esc = std::string(kSgrMuted) + "esc" + std::string(kSgrReset) + std::string(kSgrQuestionBg);
  auto const line_cols = terminal_text_columns(line);
  auto const esc_cols = terminal_text_columns(esc);
  if (line_cols + esc_cols + 3 < width)
    line += std::string(width - line_cols - esc_cols - 2, ' ') + esc;
  return modal_line(std::move(line), width);
}

std::string modal_search_line(QuestionPromptView const& prompt, std::size_t width)
{
  std::string query = sanitize_terminal_text(prompt.custom_text);
  if (query.empty())
    query = std::string(kSgrMuted) + "Search" + std::string(kSgrReset) + std::string(kSgrQuestionBg);
  query += std::string(kSgrAccent) + "█" + std::string(kSgrReset) + std::string(kSgrQuestionBg);
  return modal_line("Search: " + std::move(query), width);
}

std::string modal_option_line(QuestionPromptView const& prompt, std::size_t index, std::size_t width)
{
  auto const& option = prompt.options[index];
  auto const cursor = index == prompt.selected_option_index && !question_custom_owns_cursor(prompt);
  std::string text = cursor ? "› " : "  ";
  text += std::to_string(index + 1) + ". ";
  if (prompt.multiple)
    text += option.selected ? "✓ " : "· ";
  text += sanitize_terminal_text(option.label.empty() ? option.value : option.label);
  auto const style = cursor ? std::string(kSgrAccent) + std::string(kSgrBold) : std::string(kSgrMuted);
  return modal_line(style + std::move(text) + std::string(kSgrReset), width);
}

std::string modal_keys_line(QuestionPromptView const& prompt, std::size_t width)
{
  auto const copy_only =
      !prompt.options.empty() && std::ranges::all_of(prompt.options, [](auto const& option) { return copy_text_for_option(option).has_value(); });
  if (copy_only)
  {
    return modal_line(std::string(kSgrMuted) + "C copy  Esc cancel" + std::string(kSgrReset), width);
  }
  auto const search = prompt.searchable ? std::string("  Type to search  ") : std::string("  ");
  auto const shortcuts = !prompt.searchable && !prompt.allow_custom && !prompt.options.empty() ? std::string("  Letter shortcut  ") : std::string("");
  return modal_line(std::string(kSgrMuted) + "↑/↓ select  Enter confirm" + shortcuts + search + "Esc cancel" + std::string(kSgrReset), width);
}

std::string question_dock_keys(QuestionPromptView const& prompt, std::size_t width)
{
  auto const select = prompt.multiple ? std::string("Space toggle  Enter send") : std::string("Enter/1-9 select");
  std::array const candidates = {
      std::string("  ") + key_pill("Tab/arrows") + " move  " + key_pill(select) + "  " + key_pill("Esc") + " cancel",
      std::string("  ") + key_pill("Tab") + " move  " + key_pill("Enter") + " send  " + key_pill("Esc") + " cancel",
      std::string("  ") + key_pill("Enter") + " send  " + key_pill("Esc") + " cancel",
  };
  for (auto const& candidate : candidates)
  {
    if (terminal_text_columns(candidate) <= width)
      return candidate;
  }
  return fit_line_preserving_sgr(candidates.back(), width);
}

}  // namespace

std::vector<std::string> render_permission_prompt(PermissionPromptView const& prompt, std::size_t width, std::size_t max_lines)
{
  std::vector<std::string> lines;
  if (max_lines == 0)
    return lines;

  auto const actions =
      permission_dock_actions(prompt.selected_choice, prompt.allow_session_available, prompt.allow_remember_available, prompt.deny_remember_available, width);
  if (max_lines == 1)
  {
    lines.push_back(actions);
    return lines;
  }

  lines.push_back(permission_dock_header(width));
  if (max_lines == 2)
  {
    lines.push_back(actions);
    return lines;
  }

  if (max_lines <= 4)
  {
    lines.push_back(permission_dock_compact_summary(prompt, width));
    lines.push_back(actions);
    if (lines.size() < max_lines)
      lines.push_back(permission_dock_keys(prompt.allow_session_available, prompt.allow_remember_available, prompt.deny_remember_available, width));
    return lines;
  }

  if (prompt.diff_preview.empty())
  {
    lines.push_back(permission_dock_action(prompt, width));
    if (!permission_subject(prompt).empty())
      lines.push_back(permission_dock_subject(prompt, width));
  }
  else
  {
    lines.push_back(permission_dock_compact_summary(prompt, width));
  }

  constexpr std::size_t kReservedActionLines = 2;
  auto const metadata_text = permission_metadata_text(prompt);
  if (auto metadata = permission_dock_metadata(metadata_text, width); !metadata.empty() && max_lines > lines.size() + kReservedActionLines)
    lines.push_back(std::move(metadata));

  if (!prompt.diff_preview.empty() && max_lines > lines.size() + kReservedActionLines)
  {
    auto const diff_budget = max_lines - lines.size() - kReservedActionLines;
    std::vector<std::string> diff_lines;
    append_permission_diff_lines(diff_lines, prompt, width, diff_budget);
    lines.insert(lines.end(), diff_lines.begin(), diff_lines.end());
  }

  lines.push_back(actions);
  if (lines.size() < max_lines)
    lines.push_back(permission_dock_keys(prompt.allow_session_available, prompt.allow_remember_available, prompt.deny_remember_available, width));
  return lines;
}

namespace {

struct QuestionRenderRows
{
  std::vector<std::string> lines;
  std::vector<std::optional<std::size_t>> option_indices;
};

void append_question_row(QuestionRenderRows& rows, std::string line, std::optional<std::size_t> option_index = std::nullopt)
{
  rows.lines.push_back(std::move(line));
  rows.option_indices.push_back(option_index);
}

QuestionRenderRows render_question_prompt_rows(QuestionPromptView const& prompt, std::size_t width, std::size_t max_lines)
{
  QuestionRenderRows rows;
  if (max_lines == 0)
    return rows;
  append_question_row(rows, question_dock_header(prompt, width));
  if (rows.lines.size() >= max_lines)
    return rows;

  auto const content_width = width > 4 ? width - 4 : std::size_t{1};
  auto question_lines = wrap_transcript_text(sanitize_terminal_text(prompt.question), content_width);
  auto const has_option = !matching_option_indices(prompt).empty() || prompt.allow_custom;
  auto const reserved = std::size_t{1} + (has_option ? std::size_t{1} : std::size_t{0});
  auto const question_budget = max_lines > rows.lines.size() + reserved ? std::min<std::size_t>(4, max_lines - rows.lines.size() - reserved) : std::size_t{0};
  for (std::size_t index = 0; index < std::min(question_lines.size(), question_budget); ++index)
    append_question_row(rows, fit_line_preserving_sgr("  " + question_lines[index], width));

  auto const custom_rows = prompt.allow_custom ? std::size_t{1} : std::size_t{0};
  auto const option_budget = max_lines > rows.lines.size() + 1 + custom_rows ? max_lines - rows.lines.size() - 1 - custom_rows : std::size_t{0};
  auto const matches = matching_option_indices(prompt);
  auto const start = visible_option_window_start(matches, prompt.selected_option_index, option_budget);
  auto const end = std::min(matches.size(), start + option_budget);
  for (std::size_t visible = start; visible < end; ++visible)
    append_question_row(rows, question_option_line(prompt, matches[visible], width), matches[visible]);
  if (prompt.allow_custom && rows.lines.size() + 1 < max_lines)
    append_question_row(rows, question_custom_line(prompt, width));
  if (rows.lines.size() < max_lines)
    append_question_row(rows, question_dock_keys(prompt, width));
  return rows;
}

QuestionRenderRows render_question_modal_rows(QuestionPromptView const& prompt, std::size_t width, std::size_t max_lines)
{
  QuestionRenderRows rows;
  if (max_lines == 0)
    return rows;
  append_question_row(rows, question_surface_line("", width));
  if (rows.lines.size() >= max_lines)
    return rows;
  append_question_row(rows, modal_title_line(prompt, width));
  if (rows.lines.size() >= max_lines)
    return rows;
  append_question_row(rows, question_surface_line("", width));
  if (rows.lines.size() >= max_lines)
    return rows;

  if (prompt.searchable)
  {
    append_question_row(rows, modal_search_line(prompt, width));
    if (rows.lines.size() >= max_lines)
      return rows;
    append_question_row(rows, question_surface_line("", width));
    if (rows.lines.size() >= max_lines)
      return rows;
  }
  else if (!prompt.question.empty() && prompt.question != prompt.header)
  {
    auto const question_lines = modal_wrapped_lines(prompt.question, width);
    auto const reserved = std::size_t{4};
    std::size_t rendered = 0;
    for (auto const& line : question_lines)
    {
      if (rows.lines.size() + reserved >= max_lines)
        break;
      append_question_row(rows, line);
      ++rendered;
    }
    if (rendered < question_lines.size() && rows.lines.size() + reserved < max_lines)
      append_question_row(rows, modal_line(std::string(kSgrMuted) + "..." + std::string(kSgrReset), width));
    if (rows.lines.size() >= max_lines)
      return rows;
    append_question_row(rows, question_surface_line("", width));
    if (rows.lines.size() >= max_lines)
      return rows;
  }

  if (!prompt.options.empty())
  {
    auto const matches = matching_option_indices(prompt);
    auto const reserved_footer = std::size_t{2};
    auto const budget = max_lines > rows.lines.size() + reserved_footer ? max_lines - rows.lines.size() - reserved_footer : 0;
    auto const content_rows = visible_question_modal_rows(prompt, budget);
    for (auto const& row : content_rows)
    {
      if (row.option_index)
        append_question_row(rows, modal_option_line(prompt, *row.option_index, width), row.option_index);
      else
        append_question_row(rows, modal_line(std::string(kSgrWarning) + row.section + std::string(kSgrReset), width));
    }
    if (matches.empty() && rows.lines.size() + reserved_footer < max_lines)
      append_question_row(rows, modal_line(std::string(kSgrMuted) + "No matches. Press Enter to use custom provider id." + std::string(kSgrReset), width));
  }

  if (prompt.allow_custom && !prompt.searchable && rows.lines.size() < max_lines)
    append_question_row(rows, question_custom_line(prompt, width));
  if (rows.lines.size() < max_lines)
    append_question_row(rows, question_surface_line("", width));
  if (rows.lines.size() < max_lines)
    append_question_row(rows, modal_keys_line(prompt, width));
  while (rows.lines.size() < max_lines) append_question_row(rows, question_surface_line("", width));
  return rows;
}

}  // namespace

std::vector<std::string> render_question_prompt(QuestionPromptView const& prompt, std::size_t width, std::size_t max_lines)
{
  auto lines = render_question_prompt_rows(prompt, width, max_lines).lines;
  for (auto& line : lines) line = question_surface_line(std::move(line), width);
  return lines;
}

std::vector<std::string> render_question_modal(QuestionPromptView const& prompt, std::size_t width, std::size_t max_lines)
{
  auto lines = render_question_modal_rows(prompt, width, max_lines).lines;
  for (auto& line : lines) line = question_surface_line(std::move(line), width);
  return lines;
}

std::optional<std::size_t> question_option_for_dock_row(QuestionPromptView const& prompt, std::size_t row, std::size_t width, std::size_t max_lines)
{
  auto const rows = render_question_prompt_rows(prompt, width, max_lines);
  return row < rows.option_indices.size() ? rows.option_indices[row] : std::nullopt;
}

std::optional<std::size_t> question_option_for_modal_row(QuestionPromptView const& prompt, std::size_t row, std::size_t width, std::size_t max_lines)
{
  auto const rows = render_question_modal_rows(prompt, width, max_lines);
  return row < rows.option_indices.size() ? rows.option_indices[row] : std::nullopt;
}

}  // namespace detail

PermissionPromptRememberAvailability permission_prompt_remember_availability(ava::permissions::PermissionPrompt const& prompt,
                                                                             bool rule_storage_available) noexcept
{
  return PermissionPromptRememberAvailability{
      .allow_remember_available = rule_storage_available && ava::permissions::command_prompt_allows_persistent_allow(prompt),
      .deny_remember_available = rule_storage_available};
}

PermissionPromptInputResult handle_permission_prompt_input(PermissionPromptChoice selected_choice, InputEvent event, bool allow_session_available,
                                                           bool allow_remember_available, bool deny_remember_available)
{
  switch (event.key)
  {
    case Key::Character:
      if (event.character == 'a' || event.character == 'A')
      {
        return {.selected_choice = PermissionPromptChoice::Allow, .action = PermissionPromptInputAction::ResolveAllow};
      }
      if ((event.character == 's' || event.character == 'S') && allow_session_available)
      {
        return {.selected_choice = PermissionPromptChoice::AllowSession, .action = PermissionPromptInputAction::ResolveAllowSession};
      }
      if (event.character == 'd' || event.character == 'D')
      {
        return {.selected_choice = PermissionPromptChoice::Deny, .action = PermissionPromptInputAction::ResolveDeny};
      }
      if (event.character == ' ')
      {
        return {.selected_choice = selected_choice, .action = detail::resolve_permission_choice_action(selected_choice)};
      }
      if (event.character == 'r' || event.character == 'R')
      {
        if (detail::permission_choice_is_remember(selected_choice))
          return {.selected_choice = detail::one_shot_permission_choice(selected_choice), .action = PermissionPromptInputAction::Redraw};
        if (detail::permission_choice_can_be_remembered(selected_choice, allow_remember_available, deny_remember_available))
          return {.selected_choice = detail::remembered_permission_choice(selected_choice), .action = PermissionPromptInputAction::Redraw};
      }
      break;
    case Key::Space:
      return {.selected_choice = selected_choice, .action = detail::resolve_permission_choice_action(selected_choice)};
    case Key::Enter:
      return {.selected_choice = selected_choice, .action = detail::resolve_permission_choice_action(selected_choice)};
    case Key::Tab:
      return {.selected_choice = detail::next_permission_choice(selected_choice, allow_session_available, allow_remember_available, deny_remember_available),
              .action = PermissionPromptInputAction::Redraw};
    case Key::ArrowLeft:
    case Key::ArrowUp:
    case Key::MouseWheelUp:
      return {
          .selected_choice = detail::previous_permission_choice(selected_choice, allow_session_available, allow_remember_available, deny_remember_available),
          .action = PermissionPromptInputAction::Redraw};
    case Key::ArrowRight:
    case Key::ArrowDown:
    case Key::MouseWheelDown:
      return {.selected_choice = detail::next_permission_choice(selected_choice, allow_session_available, allow_remember_available, deny_remember_available),
              .action = PermissionPromptInputAction::Redraw};
    case Key::Escape:
    case Key::CtrlC:
    case Key::CtrlD:
      return {.selected_choice = PermissionPromptChoice::Deny, .action = PermissionPromptInputAction::ResolveDeny};
    case Key::Backspace:
    case Key::ShiftBackspace:
    case Key::CtrlBackspace:
    case Key::Delete:
    case Key::ShiftDelete:
    case Key::Insert:
    case Key::Clear:
    case Key::ShiftArrowUp:
    case Key::ShiftArrowDown:
    case Key::ShiftArrowLeft:
    case Key::ShiftArrowRight:
    case Key::ShiftCtrlArrowLeft:
    case Key::ShiftCtrlArrowRight:
    case Key::ShiftAltArrowLeft:
    case Key::ShiftAltArrowRight:
    case Key::CtrlArrowLeft:
    case Key::CtrlArrowRight:
    case Key::AltArrowLeft:
    case Key::AltArrowRight:
    case Key::CtrlA:
    case Key::CtrlB:
    case Key::CtrlE:
    case Key::CtrlF:
    case Key::CtrlG:
    case Key::CtrlH:
    case Key::CtrlK:
    case Key::CtrlL:
    case Key::CtrlMinus:
    case Key::CtrlSlash:
    case Key::Ctrl0:
    case Key::Ctrl1:
    case Key::Ctrl2:
    case Key::Ctrl3:
    case Key::Ctrl4:
    case Key::Ctrl5:
    case Key::Ctrl6:
    case Key::Ctrl7:
    case Key::Ctrl8:
    case Key::Ctrl9:
    case Key::CtrlN:
    case Key::CtrlO:
    case Key::CtrlP:
    case Key::CtrlSpace:
    case Key::CtrlShiftP:
    case Key::CtrlR:
    case Key::CtrlS:
    case Key::CtrlT:
    case Key::CtrlU:
    case Key::CtrlV:
    case Key::CtrlW:
    case Key::CtrlX:
    case Key::CtrlY:
    case Key::CtrlZ:
    case Key::CtrlRightBracket:
    case Key::AltBackspace:
    case Key::AltArrowUp:
    case Key::AltArrowDown:
    case Key::AltB:
    case Key::AltD:
    case Key::AltDelete:
    case Key::AltF:
    case Key::AltH:
    case Key::AltJ:
    case Key::AltK:
    case Key::AltL:
    case Key::AltW:
    case Key::CtrlAltRightBracket:
    case Key::AltY:
    case Key::PageUp:
    case Key::PageDown:
    case Key::Home:
    case Key::End:
    case Key::CtrlHome:
    case Key::CtrlEnd:
    case Key::ShiftHome:
    case Key::ShiftEnd:
    case Key::ShiftCtrlHome:
    case Key::ShiftCtrlEnd:
    case Key::MouseLeftClick:
    case Key::MouseLeftDrag:
    case Key::MouseLeftRelease:
    case Key::ShiftTab:
    case Key::ShiftL:
    case Key::ShiftT:
    case Key::ShiftEnter:
    case Key::CtrlEnter:
    case Key::AltEnter:
    case Key::F1:
    case Key::F2:
    case Key::F3:
    case Key::F4:
    case Key::F5:
    case Key::F6:
    case Key::F7:
    case Key::F8:
    case Key::F9:
    case Key::F10:
    case Key::F11:
    case Key::F12:
    case Key::Unknown:
      break;
  }
  return {.selected_choice = selected_choice, .action = PermissionPromptInputAction::None};
}

QuestionPromptInputResult activate_question_option(QuestionPromptView const& prompt, std::size_t option_index)
{
  auto result = QuestionPromptInputResult{.selected_option_index = prompt.selected_option_index,
                                          .options = prompt.options,
                                          .custom_text = prompt.custom_text,
                                          .copy_text = {},
                                          .action = QuestionPromptInputAction::None};
  if (option_index >= result.options.size())
    return result;

  result.selected_option_index = option_index;
  if (auto copy_text = detail::copy_text_for_option(result.options[option_index]))
  {
    result.copy_text = std::string(*copy_text);
    result.action = QuestionPromptInputAction::Copy;
    return result;
  }
  if (prompt.multiple)
  {
    result.options[option_index].selected = !result.options[option_index].selected;
    result.action = QuestionPromptInputAction::Redraw;
    return result;
  }

  for (auto& option : result.options) option.selected = false;
  result.options[option_index].selected = true;
  result.custom_text.clear();
  result.action = QuestionPromptInputAction::Resolve;
  return result;
}

QuestionPromptInputResult handle_question_prompt_input(QuestionPromptView const& prompt, InputEvent event)
{
  auto result = QuestionPromptInputResult{.selected_option_index = prompt.selected_option_index,
                                          .options = prompt.options,
                                          .custom_text = prompt.custom_text,
                                          .copy_text = {},
                                          .action = QuestionPromptInputAction::None};
  auto const has_options = !result.options.empty();
  auto matching_indices = [&]() {
    QuestionPromptView current = prompt;
    current.options = result.options;
    current.selected_option_index = result.selected_option_index;
    current.custom_text = result.custom_text;
    return detail::matching_option_indices(current);
  };
  auto clamp_selection = [&]() {
    if (!has_options)
    {
      result.selected_option_index = 0;
      return;
    }
    if (prompt.searchable)
    {
      auto const matches = matching_indices();
      if (matches.empty())
      {
        result.selected_option_index = 0;
        return;
      }
      if (std::ranges::find(matches, result.selected_option_index) == matches.end())
      {
        result.selected_option_index = matches.front();
      }
      return;
    }
    result.selected_option_index = std::min(result.selected_option_index, result.options.size() - 1);
  };
  auto move_selection = [&](std::ptrdiff_t delta, bool wrap) {
    auto const matches = matching_indices();
    if (matches.empty())
      return;
    auto const current = std::ranges::find(matches, result.selected_option_index);
    auto const visible = current == matches.end() ? std::ptrdiff_t{0} : std::distance(matches.begin(), current);
    auto next = visible + delta;
    auto const count = static_cast<std::ptrdiff_t>(matches.size());
    if (wrap)
    {
      next %= count;
      if (next < 0)
        next += count;
    }
    else
    {
      next = std::clamp(next, std::ptrdiff_t{0}, count - 1);
    }
    result.selected_option_index = matches[static_cast<std::size_t>(next)];
  };
  auto toggle_selected = [&]() {
    if (!has_options)
      return;
    clamp_selection();
    if (prompt.searchable && matching_indices().empty())
      return;
    if (prompt.multiple)
    {
      result.options[result.selected_option_index].selected = !result.options[result.selected_option_index].selected;
    }
    else
    {
      for (auto& option : result.options) option.selected = false;
      result.options[result.selected_option_index].selected = true;
    }
  };
  auto activate_option = [&]() {
    if (!has_options)
      return false;
    clamp_selection();
    if (prompt.searchable && matching_indices().empty())
      return false;
    if (auto copy_text = detail::copy_text_for_option(result.options[result.selected_option_index]))
    {
      result.copy_text = std::string(*copy_text);
      result.action = QuestionPromptInputAction::Copy;
      return true;
    }
    toggle_selected();
    result.action = prompt.multiple ? QuestionPromptInputAction::Redraw : QuestionPromptInputAction::Resolve;
    return true;
  };
  auto clear_single_selection_for_custom_text = [&]() {
    if (prompt.multiple)
      return;
    for (auto& option : result.options) option.selected = false;
  };
  auto character_text = [&]() -> std::string {
    if (!event.text.empty())
      return event.text;
    if (event.character == '\0')
      return {};
    return std::string(1, event.character);
  };

  clamp_selection();
  switch (event.key)
  {
    case Key::Character:
      if (prompt.searchable)
      {
        if (auto text = character_text(); !text.empty())
        {
          result.custom_text += text;
          clamp_selection();
          result.action = QuestionPromptInputAction::Redraw;
          return result;
        }
        break;
      }
      if (!prompt.allow_custom && has_options)
      {
        if (auto shortcut_index = detail::option_index_for_shortcut(result, event.character))
        {
          result.selected_option_index = *shortcut_index;
          static_cast<void>(activate_option());
          return result;
        }
      }
      if (event.character >= '1' && event.character <= '9')
      {
        auto const index = static_cast<std::size_t>(event.character - '1');
        if (index < result.options.size())
        {
          result.selected_option_index = index;
          static_cast<void>(activate_option());
          return result;
        }
      }
      if (event.character == ' ')
      {
        if (prompt.allow_custom && !result.custom_text.empty())
        {
          result.custom_text.push_back(' ');
          result.action = QuestionPromptInputAction::Redraw;
          return result;
        }
        if (has_options)
        {
          static_cast<void>(activate_option());
          return result;
        }
        if (prompt.allow_custom)
        {
          result.custom_text.push_back(' ');
          result.action = QuestionPromptInputAction::Redraw;
          return result;
        }
        result.action = QuestionPromptInputAction::None;
        return result;
      }
      if (auto text = character_text(); prompt.allow_custom && !text.empty())
      {
        clear_single_selection_for_custom_text();
        result.custom_text += text;
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::Space:
      if (prompt.searchable)
      {
        result.custom_text += ' ';
        clamp_selection();
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      if (prompt.allow_custom && !result.custom_text.empty())
      {
        result.custom_text.push_back(' ');
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      if (has_options)
      {
        static_cast<void>(activate_option());
        return result;
      }
      if (prompt.allow_custom)
      {
        result.custom_text.push_back(' ');
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      result.action = QuestionPromptInputAction::None;
      return result;
    case Key::Backspace:
    case Key::ShiftBackspace:
      if ((prompt.allow_custom || prompt.searchable) && !result.custom_text.empty())
      {
        erase_last_utf8_codepoint(result.custom_text);
        clamp_selection();
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::ArrowUp:
    case Key::MouseWheelUp:
      if (has_options && !matching_indices().empty())
      {
        move_selection(-1, true);
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::ArrowDown:
    case Key::Tab:
    case Key::MouseWheelDown:
      if (has_options && !matching_indices().empty())
      {
        move_selection(1, true);
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::PageUp:
      if (has_options && !matching_indices().empty())
      {
        move_selection(-5, false);
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::PageDown:
      if (has_options && !matching_indices().empty())
      {
        move_selection(5, false);
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::Home:
    case Key::CtrlHome: {
      auto const matches = matching_indices();
      if (!matches.empty())
      {
        result.selected_option_index = matches.front();
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    }
    case Key::End:
    case Key::CtrlEnd: {
      auto const matches = matching_indices();
      if (!matches.empty())
      {
        result.selected_option_index = matches.back();
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    }
    case Key::Enter:
      if (prompt.searchable)
      {
        if (has_options && !matching_indices().empty())
        {
          if (activate_option())
            return result;
        }
        result.action = QuestionPromptInputAction::Resolve;
        return result;
      }
      if (!prompt.multiple && (!prompt.allow_custom || result.custom_text.empty()))
      {
        if (activate_option())
          return result;
      }
      if (!prompt.multiple && prompt.allow_custom && result.custom_text.empty() && !has_options)
      {
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      result.action = QuestionPromptInputAction::Resolve;
      return result;
    case Key::Escape:
    case Key::CtrlC:
    case Key::CtrlD:
      result.action = QuestionPromptInputAction::Cancel;
      return result;
    case Key::Delete:
    case Key::CtrlBackspace:
    case Key::ShiftDelete:
    case Key::Insert:
    case Key::Clear:
    case Key::CtrlA:
    case Key::CtrlB:
    case Key::CtrlE:
    case Key::CtrlF:
    case Key::CtrlG:
    case Key::CtrlH:
    case Key::CtrlK:
    case Key::CtrlL:
    case Key::CtrlMinus:
    case Key::CtrlSlash:
    case Key::Ctrl0:
    case Key::Ctrl1:
    case Key::Ctrl2:
    case Key::Ctrl3:
    case Key::Ctrl4:
    case Key::Ctrl5:
    case Key::Ctrl6:
    case Key::Ctrl7:
    case Key::Ctrl8:
    case Key::Ctrl9:
    case Key::CtrlN:
    case Key::CtrlO:
    case Key::CtrlP:
    case Key::CtrlSpace:
    case Key::CtrlShiftP:
    case Key::CtrlR:
    case Key::CtrlS:
    case Key::CtrlT:
    case Key::CtrlU:
    case Key::CtrlV:
    case Key::CtrlW:
    case Key::CtrlX:
    case Key::CtrlY:
    case Key::CtrlZ:
    case Key::CtrlRightBracket:
    case Key::AltBackspace:
    case Key::AltArrowUp:
    case Key::AltArrowDown:
    case Key::AltB:
    case Key::AltD:
    case Key::AltDelete:
    case Key::AltF:
    case Key::AltH:
    case Key::AltJ:
    case Key::AltK:
    case Key::AltL:
    case Key::AltW:
    case Key::CtrlAltRightBracket:
    case Key::AltY:
    case Key::ArrowLeft:
    case Key::ArrowRight:
    case Key::ShiftArrowUp:
    case Key::ShiftArrowDown:
    case Key::ShiftArrowLeft:
    case Key::ShiftArrowRight:
    case Key::ShiftCtrlArrowLeft:
    case Key::ShiftCtrlArrowRight:
    case Key::ShiftAltArrowLeft:
    case Key::ShiftAltArrowRight:
    case Key::CtrlArrowLeft:
    case Key::CtrlArrowRight:
    case Key::AltArrowLeft:
    case Key::AltArrowRight:
    case Key::ShiftHome:
    case Key::ShiftEnd:
    case Key::ShiftCtrlHome:
    case Key::ShiftCtrlEnd:
    case Key::MouseLeftClick:
    case Key::MouseLeftDrag:
    case Key::MouseLeftRelease:
    case Key::ShiftTab:
    case Key::ShiftL:
    case Key::ShiftT:
    case Key::ShiftEnter:
    case Key::CtrlEnter:
    case Key::AltEnter:
    case Key::F1:
    case Key::F2:
    case Key::F3:
    case Key::F4:
    case Key::F5:
    case Key::F6:
    case Key::F7:
    case Key::F8:
    case Key::F9:
    case Key::F10:
    case Key::F11:
    case Key::F12:
    case Key::Unknown:
      break;
  }
  return result;
}

}  // namespace ava::tui
