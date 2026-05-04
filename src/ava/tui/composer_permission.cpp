#include <algorithm>
#include <array>
#include <cctype>

#include "ava/tui/composer_internal.h"

namespace ava::tui {
namespace detail {
namespace {

std::string render_permission_choice(std::string_view label, bool selected) {
  std::string text = selected ? "> " : "  ";
  text += label;
  if (!selected) return text;
  return std::string(kReverseVideo) + text + std::string(kSgrReset);
}

std::string render_compact_permission_choice(std::string_view label, bool selected) {
  std::string text = selected ? "> " : "  ";
  text += label;
  if (!selected) return text;
  return std::string(kReverseVideo) + text + std::string(kSgrReset);
}

std::string permission_dock_header(std::size_t width) {
  if (width < 8) {
    return fit_line_preserving_sgr(std::string(kSgrBold) + std::string(kSgrError) + "PERM" + std::string(kSgrReset),
                                   width);
  }
  std::string text = "  -- PERMISSION REQUIRED";
  auto text_cols = terminal_text_columns(text);
  if (text_cols < width) text += std::string(width - text_cols, '-');
  text = fit_line(text, width);

  auto pos = text.find("PERMISSION REQUIRED");
  if (pos != std::string::npos) {
    std::string result;
    result += std::string(kSgrDim) + text.substr(0, pos) + std::string(kSgrReset);
    result += std::string(kSgrBold) + std::string(kSgrError) + "PERMISSION REQUIRED" + std::string(kSgrReset);
    if (pos + 19 < text.size()) {
      result += std::string(kSgrDim) + text.substr(pos + 19) + std::string(kSgrReset);
    }
    return result;
  }
  return std::string(kSgrDim) + text + std::string(kSgrReset);
}

std::string permission_dock_summary(const PermissionPromptView& prompt, std::size_t width) {
  std::string summary = "  ";
  if (!prompt.tool_name.empty()) {
    summary += std::string(kSgrBold) + sanitize_terminal_text(prompt.tool_name) + std::string(kSgrReset);
    if (!prompt.command.empty() || !prompt.target.empty() || !prompt.operation.empty()) {
      summary += ": ";
    }
  }

  std::string detail_text;
  if (!prompt.command.empty()) {
    detail_text = sanitize_terminal_text(prompt.command);
  } else if (!prompt.target.empty()) {
    detail_text = sanitize_terminal_text(prompt.target);
  } else if (!prompt.operation.empty()) {
    detail_text = sanitize_terminal_text(prompt.operation);
  }

  if (!detail_text.empty()) {
    summary += detail_text;
  }

  if (!prompt.reason.empty()) {
    auto reason = sanitize_terminal_text(prompt.reason);
    auto candidate = summary + "  " + std::string(kSgrDim) + reason + std::string(kSgrReset);
    if (terminal_text_columns(candidate) <= width) {
      summary = candidate;
    }
  }

  return fit_line_preserving_sgr(summary, width);
}

std::string key_pill(std::string_view key) { return std::string(kSgrBold) + std::string(key) + std::string(kSgrReset); }

std::string permission_dock_actions(PermissionPromptChoice selected, std::size_t width) {
  const std::array candidates = {
      std::string("  ") + render_permission_choice("[Deny]", selected == PermissionPromptChoice::Deny) + "  " +
          render_permission_choice("[Allow once]", selected == PermissionPromptChoice::Allow),
      std::string("  ") + render_compact_permission_choice("[D]", selected == PermissionPromptChoice::Deny) + " " +
          render_compact_permission_choice("[A]", selected == PermissionPromptChoice::Allow),
      std::string("  ") + render_permission_choice("[Deny]", selected == PermissionPromptChoice::Deny) + " " +
          render_permission_choice("[Allow]", selected == PermissionPromptChoice::Allow),
      std::string("  ") + render_permission_choice("[D]", selected == PermissionPromptChoice::Deny) + " " +
          render_permission_choice("[A]", selected == PermissionPromptChoice::Allow),
  };

  for (const auto& candidate : candidates) {
    if (terminal_text_columns(candidate) <= width) return candidate;
  }
  return fit_line_preserving_sgr(candidates.back(), width);
}

std::string permission_dock_keys(std::size_t width) {
  const std::array candidates = {
      std::string("  ") + key_pill("A") + " allow once  " + key_pill("D") + " deny  " + key_pill("Enter") +
          " confirm  " + key_pill("Esc") + " deny  " + key_pill("Tab/arrows") + " move",
      std::string("  ") + key_pill("A") + " allow  " + key_pill("D") + " deny  " + key_pill("Enter") + " confirm  " +
          key_pill("Esc") + " deny  " + key_pill("Tab") + " move",
      std::string("  ") + key_pill("A") + " allow  " + key_pill("D") + " deny  " + key_pill("Enter") + " ok  " +
          key_pill("Esc") + " no  " + key_pill("Tab") + " move",
      std::string("  ") + key_pill("A") + " allow  " + key_pill("D") + " deny  " + key_pill("Enter/Esc") + " ok/no",
      std::string("  ") + key_pill("A") + "=allow " + key_pill("D") + "=deny",
  };

  for (const auto& candidate : candidates) {
    if (terminal_text_columns(candidate) <= width) return candidate;
  }
  return fit_line_preserving_sgr(std::string("  ") + key_pill("A") + "=allow " + key_pill("D") + "=deny", width);
}

void append_permission_diff_lines(std::vector<std::string>& lines, const PermissionPromptView& prompt,
                                  std::size_t width, std::size_t budget) {
  if (prompt.diff_preview.empty() || budget == 0) return;
  const auto prefix = std::string("  ");
  lines.push_back(fit_line_preserving_sgr(prefix + std::string(kSgrDim) + "diff:" + std::string(kSgrReset), width));
  if (lines.size() >= budget) return;

  const auto line_prefix = std::string("    ");
  const auto diff_line_budget = prompt.diff_truncated && budget > lines.size() ? budget - 1 : budget;
  for (const auto& raw_line : split_lines(prompt.diff_preview)) {
    if (lines.size() >= diff_line_budget) break;
    auto sanitized = sanitize_terminal_text(raw_line);
    std::string_view sgr = kSgrMuted;
    if (!sanitized.empty() && sanitized.front() == '+') {
      sgr = kSgrSuccess;
    } else if (!sanitized.empty() && sanitized.front() == '-') {
      sgr = kSgrError;
    }
    lines.push_back(
        fit_line_preserving_sgr(line_prefix + std::string(sgr) + std::move(sanitized) + std::string(kSgrReset), width));
  }
  if (prompt.diff_truncated && lines.size() < budget) {
    lines.push_back(fit_line_preserving_sgr(
        line_prefix + std::string(kSgrWarning) + "[diff truncated]" + std::string(kSgrReset), width));
  }
}

std::string question_dock_header(const QuestionPromptView& prompt, std::size_t width) {
  auto label = prompt.header.empty() ? std::string("QUESTION") : sanitize_terminal_text(prompt.header);
  label = prompt.multiple ? label + " (multi-select)" : label;
  std::string text = "  -- " + label;
  auto text_cols = terminal_text_columns(text);
  if (text_cols < width) text += std::string(width - text_cols, '-');
  text = fit_line(text, width);
  return std::string(kSgrDim) + text + std::string(kSgrReset);
}

std::string question_option_line(const QuestionPromptView& prompt, std::size_t index, std::size_t width) {
  const auto& option = prompt.options[index];
  std::string text = "  ";
  text += index == prompt.selected_option_index ? "> " : "  ";
  text += std::to_string(index + 1) + ". ";
  text += prompt.multiple ? (option.selected ? "[x] " : "[ ] ") : "";
  text += sanitize_terminal_text(option.label.empty() ? option.value : option.label);
  if (index == prompt.selected_option_index) text = std::string(kReverseVideo) + text + std::string(kSgrReset);
  return fit_line_preserving_sgr(text, width);
}

std::string question_custom_line(const QuestionPromptView& prompt, std::size_t width) {
  std::string text = "  Custom: ";
  if (prompt.custom_text.empty()) {
    text += prompt.secret ? std::string("paste secret") : std::string("type to answer");
  } else if (prompt.secret) {
    text += std::string(std::min<std::size_t>(prompt.custom_text.size(), 64), '*');
  } else {
    text += sanitize_terminal_text(prompt.custom_text);
  }
  return fit_line_preserving_sgr(std::string(kSgrTextDimmed) + text + std::string(kSgrReset), width);
}

std::string lower_ascii(std::string_view text) {
  std::string lowered;
  lowered.reserve(text.size());
  for (const char ch : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

bool prompt_option_matches(const QuestionPromptView& prompt, std::size_t index) {
  if (!prompt.searchable || prompt.custom_text.empty()) return true;
  const auto query = lower_ascii(prompt.custom_text);
  const auto& option = prompt.options[index];
  const auto label = lower_ascii(option.label.empty() ? option.value : option.label);
  const auto value = lower_ascii(option.value);
  return label.find(query) != std::string::npos || value.find(query) != std::string::npos;
}

std::vector<std::size_t> matching_option_indices(const QuestionPromptView& prompt) {
  std::vector<std::size_t> indices;
  for (std::size_t index = 0; index < prompt.options.size(); ++index) {
    if (prompt_option_matches(prompt, index)) indices.push_back(index);
  }
  return indices;
}

std::string modal_line(std::string content, std::size_t width) {
  return composer_surface_line("  " + std::move(content), width);
}

std::string modal_title_line(const QuestionPromptView& prompt, std::size_t width) {
  const auto title =
      prompt.header.empty() ? sanitize_terminal_text(prompt.question) : sanitize_terminal_text(prompt.header);
  std::string line = std::string(kSgrBold) + title + std::string(kSgrReset) + std::string(kSgrComposerBg);
  const std::string esc = std::string(kSgrMuted) + "esc" + std::string(kSgrReset) + std::string(kSgrComposerBg);
  const auto line_cols = terminal_text_columns(line);
  const auto esc_cols = terminal_text_columns(esc);
  if (line_cols + esc_cols + 3 < width) line += std::string(width - line_cols - esc_cols - 2, ' ') + esc;
  return modal_line(std::move(line), width);
}

std::string modal_search_line(const QuestionPromptView& prompt, std::size_t width) {
  std::string query = sanitize_terminal_text(prompt.custom_text);
  if (query.empty()) query = std::string(kSgrMuted) + "Search" + std::string(kSgrReset) + std::string(kSgrComposerBg);
  query += std::string(kSgrAccent) + "█" + std::string(kSgrReset) + std::string(kSgrComposerBg);
  return modal_line("Search: " + std::move(query), width);
}

std::string modal_option_line(const QuestionPromptView& prompt, std::size_t index, std::size_t width) {
  const auto& option = prompt.options[index];
  std::string text = index == prompt.selected_option_index ? "› " : "  ";
  text += option.selected ? "✓ " : "  ";
  text += sanitize_terminal_text(option.label.empty() ? option.value : option.label);
  if (index == prompt.selected_option_index) text = std::string(kReverseVideo) + text + std::string(kSgrReset);
  return modal_line(std::move(text), width);
}

std::string modal_keys_line(const QuestionPromptView& prompt, std::size_t width) {
  const auto search = prompt.searchable ? std::string("  Type to search  ") : std::string("  ");
  return modal_line(
      std::string(kSgrMuted) + "↑/↓ select  Enter confirm" + search + "Esc cancel" + std::string(kSgrReset), width);
}

std::string question_dock_keys(const QuestionPromptView& prompt, std::size_t width) {
  const auto select = prompt.multiple ? std::string("Space toggle  Enter send") : std::string("Enter/1-9 select");
  const std::array candidates = {
      std::string("  ") + key_pill("Tab/arrows") + " move  " + key_pill(select) + "  " + key_pill("Esc") + " cancel",
      std::string("  ") + key_pill("Tab") + " move  " + key_pill("Enter") + " send  " + key_pill("Esc") + " cancel",
      std::string("  ") + key_pill("Enter") + " send  " + key_pill("Esc") + " cancel",
  };
  for (const auto& candidate : candidates) {
    if (terminal_text_columns(candidate) <= width) return candidate;
  }
  return fit_line_preserving_sgr(candidates.back(), width);
}

}  // namespace

std::vector<std::string> render_permission_prompt(const PermissionPromptView& prompt, std::size_t width,
                                                  std::size_t max_lines) {
  std::vector<std::string> lines;
  if (max_lines == 0) return lines;

  if (max_lines == 1) {
    lines.push_back(permission_dock_actions(prompt.selected_choice, width));
    return lines;
  }

  lines.push_back(permission_dock_header(width));

  if (max_lines == 2) {
    lines.push_back(permission_dock_actions(prompt.selected_choice, width));
    return lines;
  }

  lines.push_back(permission_dock_summary(prompt, width));

  if (max_lines == 3) {
    lines.push_back(permission_dock_actions(prompt.selected_choice, width));
    return lines;
  }

  constexpr std::size_t kReservedActionLines = 2;
  if (!prompt.diff_preview.empty() && max_lines > lines.size() + kReservedActionLines) {
    const auto diff_budget = max_lines - lines.size() - kReservedActionLines;
    std::vector<std::string> diff_lines;
    append_permission_diff_lines(diff_lines, prompt, width, diff_budget);
    lines.insert(lines.end(), diff_lines.begin(), diff_lines.end());
  }

  lines.push_back(permission_dock_actions(prompt.selected_choice, width));
  if (lines.size() < max_lines) {
    lines.push_back(permission_dock_keys(width));
  }
  return lines;
}

std::vector<std::string> render_question_prompt(const QuestionPromptView& prompt, std::size_t width,
                                                std::size_t max_lines) {
  std::vector<std::string> lines;
  if (max_lines == 0) return lines;
  lines.push_back(question_dock_header(prompt, width));
  if (lines.size() >= max_lines) return lines;
  lines.push_back(fit_line_preserving_sgr("  " + sanitize_terminal_text(prompt.question), width));
  if (lines.size() >= max_lines) return lines;

  const auto option_budget =
      prompt.allow_custom && max_lines > lines.size() + 2 ? max_lines - lines.size() - 2 : max_lines - lines.size() - 1;
  const auto option_count = std::min(prompt.options.size(), option_budget);
  for (std::size_t index = 0; index < option_count; ++index) {
    lines.push_back(question_option_line(prompt, index, width));
  }
  if (option_count < prompt.options.size() && lines.size() < max_lines) {
    lines.push_back(fit_line_preserving_sgr("  ... more options", width));
  }
  if (prompt.allow_custom && lines.size() < max_lines) {
    lines.push_back(question_custom_line(prompt, width));
  }
  if (lines.size() < max_lines) {
    lines.push_back(question_dock_keys(prompt, width));
  }
  return lines;
}

std::vector<std::string> render_question_modal(const QuestionPromptView& prompt, std::size_t width,
                                               std::size_t max_lines) {
  std::vector<std::string> lines;
  if (max_lines == 0) return lines;
  lines.push_back(composer_surface_line("", width));
  if (lines.size() >= max_lines) return lines;
  lines.push_back(modal_title_line(prompt, width));
  if (lines.size() >= max_lines) return lines;
  lines.push_back(composer_surface_line("", width));
  if (lines.size() >= max_lines) return lines;

  if (prompt.searchable) {
    lines.push_back(modal_search_line(prompt, width));
    if (lines.size() >= max_lines) return lines;
    lines.push_back(composer_surface_line("", width));
    if (lines.size() >= max_lines) return lines;
  } else if (!prompt.question.empty() && prompt.question != prompt.header) {
    lines.push_back(modal_line(sanitize_terminal_text(prompt.question), width));
    if (lines.size() >= max_lines) return lines;
    lines.push_back(composer_surface_line("", width));
    if (lines.size() >= max_lines) return lines;
  }

  if (!prompt.options.empty()) {
    if (prompt.searchable) {
      lines.push_back(modal_line(std::string(kSgrWarning) + "Popular" + std::string(kSgrReset), width));
      if (lines.size() >= max_lines) return lines;
    }
    const auto matches = matching_option_indices(prompt);
    const auto reserved_footer = std::size_t{2};
    const auto budget = max_lines > lines.size() + reserved_footer ? max_lines - lines.size() - reserved_footer : 0;
    const auto count = std::min(matches.size(), budget);
    std::size_t rendered_options = 0;
    for (std::size_t visible = 0; visible < count && lines.size() + reserved_footer < max_lines; ++visible) {
      if (prompt.searchable && visible == 4 && lines.size() + reserved_footer + 1 < max_lines) {
        lines.push_back(modal_line(std::string(kSgrWarning) + "Other" + std::string(kSgrReset), width));
      }
      lines.push_back(modal_option_line(prompt, matches[visible], width));
      ++rendered_options;
    }
    if (rendered_options == 0 && lines.size() < max_lines) {
      lines.push_back(modal_line(
          std::string(kSgrMuted) + "No matches. Press Enter to use custom provider id." + std::string(kSgrReset),
          width));
    } else if (rendered_options < matches.size() && lines.size() < max_lines) {
      lines.push_back(modal_line(std::string(kSgrMuted) + "..." + std::string(kSgrReset), width));
    }
  }

  if (prompt.allow_custom && !prompt.searchable && lines.size() < max_lines) {
    lines.push_back(question_custom_line(prompt, width));
  }
  if (lines.size() < max_lines) lines.push_back(composer_surface_line("", width));
  if (lines.size() < max_lines) lines.push_back(modal_keys_line(prompt, width));
  while (lines.size() < max_lines) lines.push_back(composer_surface_line("", width));
  return lines;
}

}  // namespace detail

PermissionPromptInputResult handle_permission_prompt_input(PermissionPromptChoice selected_choice, InputEvent event) {
  switch (event.key) {
    case Key::Character:
      if (event.character == 'a' || event.character == 'A') {
        return {.selected_choice = PermissionPromptChoice::Allow, .action = PermissionPromptInputAction::ResolveAllow};
      }
      if (event.character == 'd' || event.character == 'D') {
        return {.selected_choice = PermissionPromptChoice::Deny, .action = PermissionPromptInputAction::ResolveDeny};
      }
      if (event.character == ' ') {
        return {.selected_choice = selected_choice,
                .action = selected_choice == PermissionPromptChoice::Allow ? PermissionPromptInputAction::ResolveAllow
                                                                           : PermissionPromptInputAction::ResolveDeny};
      }
      break;
    case Key::Enter:
      return {.selected_choice = selected_choice,
              .action = selected_choice == PermissionPromptChoice::Allow ? PermissionPromptInputAction::ResolveAllow
                                                                         : PermissionPromptInputAction::ResolveDeny};
    case Key::Tab:
      return {.selected_choice = selected_choice == PermissionPromptChoice::Deny ? PermissionPromptChoice::Allow
                                                                                 : PermissionPromptChoice::Deny,
              .action = PermissionPromptInputAction::Redraw};
    case Key::ArrowLeft:
      return {.selected_choice = PermissionPromptChoice::Deny, .action = PermissionPromptInputAction::Redraw};
    case Key::ArrowRight:
      return {.selected_choice = PermissionPromptChoice::Allow, .action = PermissionPromptInputAction::Redraw};
    case Key::Escape:
    case Key::CtrlC:
    case Key::CtrlD:
      return {.selected_choice = PermissionPromptChoice::Deny, .action = PermissionPromptInputAction::ResolveDeny};
    case Key::Backspace:
    case Key::CtrlA:
    case Key::CtrlB:
    case Key::CtrlE:
    case Key::CtrlF:
    case Key::CtrlK:
    case Key::CtrlT:
    case Key::CtrlU:
    case Key::CtrlW:
    case Key::CtrlY:
    case Key::CtrlZ:
    case Key::ArrowUp:
    case Key::ArrowDown:
    case Key::PageUp:
    case Key::PageDown:
    case Key::MouseWheelUp:
    case Key::MouseWheelDown:
    case Key::MouseLeftClick:
    case Key::ShiftEnter:
    case Key::Unknown:
      break;
  }
  return {.selected_choice = selected_choice, .action = PermissionPromptInputAction::None};
}

QuestionPromptInputResult handle_question_prompt_input(const QuestionPromptView& prompt, InputEvent event) {
  auto result = QuestionPromptInputResult{.selected_option_index = prompt.selected_option_index,
                                          .options = prompt.options,
                                          .custom_text = prompt.custom_text,
                                          .action = QuestionPromptInputAction::None};
  const auto has_options = !result.options.empty();
  auto matching_indices = [&]() {
    QuestionPromptView current = prompt;
    current.options = result.options;
    current.selected_option_index = result.selected_option_index;
    current.custom_text = result.custom_text;
    return detail::matching_option_indices(current);
  };
  auto clamp_selection = [&]() {
    if (!has_options) {
      result.selected_option_index = 0;
      return;
    }
    if (prompt.searchable) {
      const auto matches = matching_indices();
      if (matches.empty()) {
        result.selected_option_index = 0;
        return;
      }
      if (std::ranges::find(matches, result.selected_option_index) == matches.end()) {
        result.selected_option_index = matches.front();
      }
      return;
    }
    result.selected_option_index = std::min(result.selected_option_index, result.options.size() - 1);
  };
  auto move_search_selection = [&](int delta) {
    const auto matches = matching_indices();
    if (matches.empty()) return;
    const auto current = std::ranges::find(matches, result.selected_option_index);
    auto visible = current == matches.end() ? std::size_t{0} : static_cast<std::size_t>(current - matches.begin());
    if (delta < 0) {
      visible = visible == 0 ? matches.size() - 1 : visible - 1;
    } else {
      visible = (visible + 1) % matches.size();
    }
    result.selected_option_index = matches[visible];
  };
  auto toggle_selected = [&]() {
    if (!has_options) return;
    clamp_selection();
    if (prompt.searchable && matching_indices().empty()) return;
    if (prompt.multiple) {
      result.options[result.selected_option_index].selected = !result.options[result.selected_option_index].selected;
    } else {
      for (auto& option : result.options) option.selected = false;
      result.options[result.selected_option_index].selected = true;
    }
  };
  auto clear_single_selection_for_custom_text = [&]() {
    if (prompt.multiple) return;
    for (auto& option : result.options) option.selected = false;
  };
  auto character_text = [&]() -> std::string {
    if (!event.text.empty()) return event.text;
    if (event.character == '\0') return {};
    return std::string(1, event.character);
  };

  clamp_selection();
  switch (event.key) {
    case Key::Character:
      if (prompt.searchable) {
        if (auto text = character_text(); !text.empty()) {
          result.custom_text += text;
          clamp_selection();
          result.action = QuestionPromptInputAction::Redraw;
          return result;
        }
        break;
      }
      if (event.character >= '1' && event.character <= '9') {
        const auto index = static_cast<std::size_t>(event.character - '1');
        if (index < result.options.size()) {
          result.selected_option_index = index;
          toggle_selected();
          result.action = prompt.multiple ? QuestionPromptInputAction::Redraw : QuestionPromptInputAction::Resolve;
          return result;
        }
      }
      if (event.character == ' ') {
        if (prompt.allow_custom && !result.custom_text.empty()) {
          result.custom_text.push_back(' ');
          result.action = QuestionPromptInputAction::Redraw;
          return result;
        }
        if (has_options) {
          toggle_selected();
          result.action = prompt.multiple ? QuestionPromptInputAction::Redraw : QuestionPromptInputAction::Resolve;
          return result;
        }
        if (prompt.allow_custom) {
          result.custom_text.push_back(' ');
          result.action = QuestionPromptInputAction::Redraw;
          return result;
        }
        result.action = QuestionPromptInputAction::None;
        return result;
      }
      if (auto text = character_text(); prompt.allow_custom && !text.empty()) {
        clear_single_selection_for_custom_text();
        result.custom_text += text;
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::Backspace:
      if ((prompt.allow_custom || prompt.searchable) && !result.custom_text.empty()) {
        erase_last_utf8_codepoint(result.custom_text);
        clamp_selection();
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::ArrowUp:
      if (prompt.searchable && has_options) {
        move_search_selection(-1);
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      if (has_options && result.selected_option_index > 0) {
        --result.selected_option_index;
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::ArrowDown:
    case Key::Tab:
      if (prompt.searchable && has_options) {
        move_search_selection(1);
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      if (has_options) {
        result.selected_option_index = (result.selected_option_index + 1) % result.options.size();
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::Enter:
      if (prompt.searchable) {
        if (has_options && !matching_indices().empty()) toggle_selected();
        result.action = QuestionPromptInputAction::Resolve;
        return result;
      }
      if (!prompt.multiple && (!prompt.allow_custom || result.custom_text.empty())) toggle_selected();
      result.action = QuestionPromptInputAction::Resolve;
      return result;
    case Key::Escape:
    case Key::CtrlC:
    case Key::CtrlD:
      result.action = QuestionPromptInputAction::Cancel;
      return result;
    case Key::CtrlA:
    case Key::CtrlB:
    case Key::CtrlE:
    case Key::CtrlF:
    case Key::CtrlK:
    case Key::CtrlT:
    case Key::CtrlU:
    case Key::CtrlW:
    case Key::CtrlY:
    case Key::CtrlZ:
    case Key::ArrowLeft:
    case Key::ArrowRight:
    case Key::PageUp:
    case Key::PageDown:
    case Key::MouseWheelUp:
    case Key::MouseWheelDown:
    case Key::MouseLeftClick:
    case Key::ShiftEnter:
    case Key::Unknown:
      break;
  }
  return result;
}

}  // namespace ava::tui
