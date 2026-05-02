#include <algorithm>
#include <array>

#include "ava/tui/composer_internal.h"

namespace ava::tui {
namespace detail {
namespace {

std::string render_permission_choice(std::string_view label, bool selected, bool announce_selected = false) {
  std::string text = selected ? "> " : "  ";
  text += label;
  if (selected && announce_selected) text += " (selected)";
  if (!selected) return text;
  return std::string(kReverseVideo) + text + std::string(kSgrReset);
}

std::string render_compact_permission_choice(std::string_view label, bool selected) {
  std::string text = selected ? "> " : "  ";
  text += label;
  if (selected) text += " sel";
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
      std::string("  ") + render_permission_choice("[Deny]", selected == PermissionPromptChoice::Deny, true) + "  " +
          render_permission_choice("[Allow once]", selected == PermissionPromptChoice::Allow, true),
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
  text += prompt.custom_text.empty() ? std::string("type to answer") : sanitize_terminal_text(prompt.custom_text);
  return fit_line_preserving_sgr(std::string(kSgrTextDimmed) + text + std::string(kSgrReset), width);
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

  lines.push_back(permission_dock_actions(prompt.selected_choice, width));
  if (max_lines >= 4) {
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
  auto clamp_selection = [&]() {
    if (!has_options) {
      result.selected_option_index = 0;
      return;
    }
    result.selected_option_index = std::min(result.selected_option_index, result.options.size() - 1);
  };
  auto toggle_selected = [&]() {
    if (!has_options) return;
    clamp_selection();
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
      if (prompt.allow_custom && !result.custom_text.empty()) {
        erase_last_utf8_codepoint(result.custom_text);
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::ArrowUp:
      if (has_options && result.selected_option_index > 0) {
        --result.selected_option_index;
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::ArrowDown:
    case Key::Tab:
      if (has_options) {
        result.selected_option_index = (result.selected_option_index + 1) % result.options.size();
        result.action = QuestionPromptInputAction::Redraw;
        return result;
      }
      break;
    case Key::Enter:
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
