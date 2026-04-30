#include "ava/tui/composer_internal.h"

#include <array>

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
      std::string("  ") + key_pill("A") + " allow  " + key_pill("D") + " deny  " + key_pill("Enter") +
          " confirm  " + key_pill("Esc") + " deny  " + key_pill("Tab") + " move",
      std::string("  ") + key_pill("A") + " allow  " + key_pill("D") + " deny  " + key_pill("Enter") +
          " ok  " + key_pill("Esc") + " no  " + key_pill("Tab") + " move",
      std::string("  ") + key_pill("A") + " allow  " + key_pill("D") + " deny  " + key_pill("Enter/Esc") + " ok/no",
      std::string("  ") + key_pill("A") + "=allow " + key_pill("D") + "=deny",
  };

  for (const auto& candidate : candidates) {
    if (terminal_text_columns(candidate) <= width) return candidate;
  }
  return fit_line_preserving_sgr(std::string("  ") + key_pill("A") + "=allow " + key_pill("D") + "=deny", width);
}

}  // namespace

std::vector<std::string> render_permission_prompt(const PermissionPromptView& prompt,
                                                   std::size_t width,
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

}  // namespace detail

PermissionPromptInputResult handle_permission_prompt_input(PermissionPromptChoice selected_choice, InputEvent event) {
  switch (event.key) {
    case Key::Character:
      if (event.character == 'a' || event.character == 'A') {
        return {.selected_choice = PermissionPromptChoice::Allow,
                .action = PermissionPromptInputAction::ResolveAllow};
      }
      if (event.character == 'd' || event.character == 'D') {
        return {.selected_choice = PermissionPromptChoice::Deny, .action = PermissionPromptInputAction::ResolveDeny};
      }
      if (event.character == ' ') {
        return {.selected_choice = selected_choice,
                .action = selected_choice == PermissionPromptChoice::Allow
                              ? PermissionPromptInputAction::ResolveAllow
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

}  // namespace ava::tui
