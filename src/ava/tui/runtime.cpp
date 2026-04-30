#include "ava/tui/runtime.h"

#include <curses.h>

#include <algorithm>
#include <climits>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"

namespace ava::tui {
namespace {

constexpr std::size_t kMaxTranscriptScrollOffsetRequest = 10'000;

class SignalBlockGuard {
 public:
  SignalBlockGuard() {
    sigset_t blocked{};
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    active_ = sigprocmask(SIG_BLOCK, &blocked, &previous_) == 0;
  }

  SignalBlockGuard(const SignalBlockGuard&) = delete;
  SignalBlockGuard& operator=(const SignalBlockGuard&) = delete;

  ~SignalBlockGuard() {
    if (active_) static_cast<void>(sigprocmask(SIG_SETMASK, &previous_, nullptr));
  }

 private:
  sigset_t previous_{};
  bool active_ = false;
};

std::pair<std::size_t, std::size_t> terminal_size() {
  int height = 0;
  int width = 0;
  getmaxyx(stdscr, height, width);
  if (width > 0 && height > 0) return {static_cast<std::size_t>(width), static_cast<std::size_t>(height)};
  return {80, 24};
}

struct CursesInput {
  InputEvent event;
  std::string text;
  bool resize = false;
};

CursesInput key_input(Key key) { return CursesInput{.event = InputEvent{.key = key}, .text = {}, .resize = false}; }

CursesInput unknown_input() { return key_input(Key::Unknown); }

std::optional<std::string> encode_wide_character(wchar_t character) {
  std::mbstate_t state{};
  char buffer[MB_LEN_MAX]{};
  const auto length = std::wcrtomb(buffer, character, &state);
  if (length == static_cast<std::size_t>(-1)) return std::nullopt;
  return std::string(buffer, length);
}

CursesInput read_curses_input() {
  wint_t value = 0;
  const auto result = wget_wch(stdscr, &value);
  if (terminal_signal_received()) return key_input(Key::CtrlC);
  if (result == ERR) return unknown_input();

  if (result == KEY_CODE_YES) {
    switch (static_cast<int>(value)) {
      case KEY_ENTER:
        return key_input(Key::Enter);
      case KEY_BACKSPACE:
        return key_input(Key::Backspace);
      case KEY_UP:
        return key_input(Key::ArrowUp);
      case KEY_DOWN:
        return key_input(Key::ArrowDown);
      case KEY_LEFT:
        return key_input(Key::ArrowLeft);
      case KEY_RIGHT:
        return key_input(Key::ArrowRight);
      case KEY_PPAGE:
        return key_input(Key::PageUp);
      case KEY_NPAGE:
        return key_input(Key::PageDown);
#ifdef KEY_RESIZE
      case KEY_RESIZE:
        return CursesInput{.event = InputEvent{.key = Key::Unknown}, .text = {}, .resize = true};
#endif
#ifdef KEY_MOUSE
      case KEY_MOUSE: {
        MEVENT mouse{};
        if (getmouse(&mouse) != OK) return unknown_input();
        if ((mouse.bstate & BUTTON4_PRESSED) != 0) {
          return key_input(Key::MouseWheelUp);
        }
        if ((mouse.bstate & BUTTON5_PRESSED) != 0) {
          return key_input(Key::MouseWheelDown);
        }
        if ((mouse.bstate & BUTTON1_CLICKED) != 0) {
          return CursesInput{.event = InputEvent{.key = Key::MouseLeftClick,
                                                 .mouse_column = static_cast<std::size_t>(mouse.x + 1),
                                                 .mouse_row = static_cast<std::size_t>(mouse.y + 1)},
                             .text = {},
                             .resize = false};
        }
        return unknown_input();
      }
#endif
      default:
        return unknown_input();
    }
  }

  const auto character = static_cast<wchar_t>(value);
  if (character == L'\r') return key_input(Key::Enter);
  if (character == L'\n') return key_input(Key::ShiftEnter);
  if (character == L'\t') return key_input(Key::Tab);
  if (character == 0x1B) return key_input(Key::Escape);
  if (character == 0x03) return key_input(Key::CtrlC);
  if (character == 0x04) return key_input(Key::CtrlD);
  if (character == 0x7F || character == L'\b') return key_input(Key::Backspace);
  if (character >= 0x20) {
    auto encoded = encode_wide_character(character);
    if (!encoded) return unknown_input();
    const auto first_byte = encoded->empty() ? '\0' : (*encoded)[0];
    return CursesInput{.event = InputEvent{.key = Key::Character, .character = first_byte},
                       .text = std::move(*encoded),
                       .resize = false};
  }
  return unknown_input();
}

bool is_utf8_continuation(unsigned char byte) { return (byte & 0xC0U) == 0x80U; }

std::size_t utf8_sequence_length(unsigned char byte) {
  if ((byte & 0x80U) == 0) return 1;
  if (byte >= 0xC2U && byte <= 0xDFU) return 2;
  if ((byte & 0xF0U) == 0xE0U) return 3;
  if (byte >= 0xF0U && byte <= 0xF4U) return 4;
  return 0;
}

bool has_utf8_continuations(std::string_view text, std::size_t start, std::size_t length) {
  if (start + length > text.size()) return false;
  for (std::size_t offset = 1; offset < length; ++offset) {
    if (!is_utf8_continuation(static_cast<unsigned char>(text[start + offset]))) return false;
  }
  return true;
}

std::size_t clamp_input_cursor(std::string_view text, std::size_t cursor) {
  cursor = std::min(cursor, text.size());
  while (cursor > 0 && cursor < text.size() && is_utf8_continuation(static_cast<unsigned char>(text[cursor]))) {
    --cursor;
  }
  return cursor;
}

std::size_t previous_input_cursor(std::string_view text, std::size_t cursor) {
  cursor = clamp_input_cursor(text, cursor);
  if (cursor == 0) return 0;
  if (!is_utf8_continuation(static_cast<unsigned char>(text[cursor - 1]))) return cursor - 1;

  auto start = cursor;
  while (start > 0 && is_utf8_continuation(static_cast<unsigned char>(text[start - 1]))) {
    --start;
  }
  if (start == 0) return cursor - 1;

  const auto starter = start - 1;
  const auto expected_length = utf8_sequence_length(static_cast<unsigned char>(text[starter]));
  const auto actual_length = cursor - starter;
  if (expected_length > 1 && expected_length == actual_length) return starter;
  return cursor - 1;
}

std::size_t next_input_cursor(std::string_view text, std::size_t cursor) {
  cursor = clamp_input_cursor(text, cursor);
  if (cursor >= text.size()) return text.size();

  const auto length = utf8_sequence_length(static_cast<unsigned char>(text[cursor]));
  if (length > 1 && has_utf8_continuations(text, cursor, length)) return cursor + length;
  return cursor + 1;
}

void erase_utf8_codepoint_before_cursor(std::string& text, std::size_t& cursor) {
  cursor = clamp_input_cursor(text, cursor);
  if (cursor == 0) return;
  const auto start = previous_input_cursor(text, cursor);
  text.erase(start, cursor - start);
  cursor = start;
}

void push_transcript(ComposerSnapshot& snapshot, TranscriptItem item) {
  constexpr std::size_t kMaxTranscriptItems = 1000;
  snapshot.transcript.push_back(std::move(item));
  if (snapshot.transcript.size() > kMaxTranscriptItems) {
    snapshot.transcript.erase(
        snapshot.transcript.begin(),
        snapshot.transcript.begin() + static_cast<std::ptrdiff_t>(snapshot.transcript.size() - kMaxTranscriptItems));
  }
}

void push_history(std::vector<std::string>& history, std::string input) {
  constexpr std::size_t kMaxHistoryItems = 100;
  if (input.empty()) return;
  history.push_back(std::move(input));
  if (history.size() > kMaxHistoryItems) {
    history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(history.size() - kMaxHistoryItems));
  }
}

PermissionPromptView permission_prompt_view(const ava::permissions::PermissionPrompt& prompt) {
  PermissionPromptView view;
  view.tool_name = prompt.tool_name;
  view.operation = ava::permissions::to_string(prompt.operation);
  view.target = prompt.target_path.empty() ? std::string{} : prompt.target_path.generic_string();
  view.command = prompt.command;
  view.reason = prompt.reason;
  return view;
}

}  // namespace

int run_interactive_composer(TuiRuntimeOptions options) {
  if (!terminal_is_tty()) {
    std::cerr << "interactive TUI requires stdin and stdout to be terminals\n";
    return 1;
  }

  clear_terminal_signal();
  auto curses = CursesSession::enter();
  if (!curses) {
    std::cerr << curses.error().format() << '\n';
    return 1;
  }

  ComposerSnapshot snapshot{.mode = options.mode,
                            .provider = options.provider,
                            .model = options.model,
                            .session_id = options.session_id,
                            .input = "",
                            .status =
                                "Enter submits. Ctrl-J/Shift+Enter inserts newline. / opens commands. ↑/↓ select. "
                                "Page/wheel scroll. Esc twice clears.",
                            .transcript = {},
                            .slash_commands = std::move(options.slash_commands)};

  bool terminal_write_failed = false;
  std::vector<std::string> input_history;
  std::optional<std::size_t> history_index;
  std::string draft_input;
  std::size_t selected_slash_command_index = 0;
  std::size_t input_cursor = 0;
  std::size_t transcript_scroll_offset = 0;
  bool pending_escape_clear = false;

  auto render = [&]() -> bool {
    if (terminal_signal_received()) return false;
    bool wrote = false;
    {
      SignalBlockGuard block_signals;
      input_cursor = clamp_input_cursor(snapshot.input, input_cursor);
      snapshot.input_cursor = input_cursor;
      snapshot.selected_slash_command_index = selected_slash_command_index;
      snapshot.transcript_scroll_offset = transcript_scroll_offset;
      const auto [width, height] = terminal_size();
      snapshot.width = width;
      snapshot.height = height;
      wrote = draw_screen(snapshot);
    }
    return wrote && !terminal_signal_received();
  };
  auto insert_newline = [&]() {
    pending_escape_clear = false;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    input_cursor = clamp_input_cursor(snapshot.input, input_cursor);
    snapshot.input.insert(input_cursor, 1, '\n');
    ++input_cursor;
  };

  ava::permissions::PermissionResolver permission_resolver = [&](const ava::permissions::PermissionPrompt& prompt)
      -> ava::core::Result<ava::permissions::PermissionResolution> {
    snapshot.permission_prompt = permission_prompt_view(prompt);
    snapshot.permission_prompt->selected_choice = PermissionPromptChoice::Deny;
    snapshot.status = "permission required: A=allow D=deny Tab/Left/Right choose Enter/Space confirm Esc deny";
    static_cast<void>(beep());
    if (!render()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render permission prompt"));
    }

    auto resolve_choice =
        [&](PermissionPromptChoice selected) -> ava::core::Result<ava::permissions::PermissionResolution> {
      snapshot.permission_prompt.reset();
      if (selected == PermissionPromptChoice::Allow) {
        snapshot.status = "permission allowed once";
        if (!render()) {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
        }
        return ava::permissions::PermissionResolution::Allow;
      }
      snapshot.status = "permission denied";
      if (!render()) {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
      }
      return ava::permissions::PermissionResolution::Deny;
    };

    while (true) {
      const auto choice_input = read_curses_input();
      if (terminal_signal_received()) {
        snapshot.permission_prompt.reset();
        snapshot.status = "interrupted";
        static_cast<void>(render());
        return ava::permissions::PermissionResolution::Deny;
      }
      if (choice_input.resize) {
        if (!render()) {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render permission prompt"));
        }
        continue;
      }

      auto input_result =
          snapshot.permission_prompt
              ? handle_permission_prompt_input(snapshot.permission_prompt->selected_choice, choice_input.event)
              : PermissionPromptInputResult{};
      if (input_result.action == PermissionPromptInputAction::ResolveAllow) {
        return resolve_choice(PermissionPromptChoice::Allow);
      }
      if (input_result.action == PermissionPromptInputAction::ResolveDeny) {
        return resolve_choice(PermissionPromptChoice::Deny);
      }
      if (input_result.action == PermissionPromptInputAction::Redraw && snapshot.permission_prompt) {
        snapshot.permission_prompt->selected_choice = input_result.selected_choice;
        static_cast<void>(render());
        continue;
      }

      snapshot.status = "permission required: A=allow D=deny Tab/Left/Right choose Enter/Space confirm Esc deny";
      if (!render()) {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render permission prompt"));
      }
    }
  };

  enum class InputLoopAction { None, ContinueLoop, BreakLoop };
  auto handle_submit = [&]() -> InputLoopAction {
    pending_escape_clear = false;
    if (slash_palette_visible(snapshot.input, snapshot.slash_commands)) {
      const auto matches = filter_slash_commands(snapshot.input, snapshot.slash_commands);
      if (!matches.empty()) {
        selected_slash_command_index =
            clamp_slash_palette_selection(snapshot.input, snapshot.slash_commands, selected_slash_command_index);
        snapshot.input =
            slash_command_selection_text(snapshot.input, snapshot.slash_commands, selected_slash_command_index);
        input_cursor = snapshot.input.size();
        selected_slash_command_index = 0;
        history_index.reset();
        draft_input.clear();
        snapshot.status = "command selected - press Enter to run";
        snapshot.selected_slash_command_index = selected_slash_command_index;
        if (!render()) {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
    }
    const auto submitted = snapshot.input;
    snapshot.input.clear();
    input_cursor = 0;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    if (!submitted.empty()) {
      push_transcript(snapshot, TranscriptItem{.label = submitted.starts_with('/') ? "cmd" : "you", .text = submitted});
      push_history(input_history, submitted);
      snapshot.status = submitted.starts_with('/') ? "running command..." : "thinking...";
      if (!render()) {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      const auto result = options.on_submit ? options.on_submit(submitted, permission_resolver) : TuiSubmitResult{};
      if (terminal_signal_received()) return InputLoopAction::BreakLoop;
      for (const auto& tool : result.tool_timeline) {
        push_transcript(snapshot, TranscriptItem{.tool = tool});
      }
      for (const auto& output : result.output) {
        push_transcript(snapshot, TranscriptItem{.label = "ava", .text = output});
      }
      transcript_scroll_offset = 0;
      snapshot.status = result.output.empty() ? "ok" : "done";
      if (!render()) {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      if (result.quit) return InputLoopAction::BreakLoop;
      return InputLoopAction::ContinueLoop;
    }
    return InputLoopAction::None;
  };

  if (terminal_signal_received()) return 130;
  if (!render()) return 1;

  while (true) {
    const auto input = read_curses_input();
    if (terminal_signal_received()) break;
    if (input.resize) {
      if (!render()) {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    const auto event = input.event;
    auto scroll_up = [&](std::size_t amount) {
      pending_escape_clear = false;
      const auto remaining_scroll = transcript_scroll_offset >= kMaxTranscriptScrollOffsetRequest
                                        ? std::size_t{0}
                                        : kMaxTranscriptScrollOffsetRequest - transcript_scroll_offset;
      transcript_scroll_offset += std::min(amount, remaining_scroll);
      snapshot.status =
          remaining_scroll < amount ? "transcript: scrollback request capped" : "transcript: scrollback requested";
    };
    auto scroll_down = [&](std::size_t amount) {
      pending_escape_clear = false;
      transcript_scroll_offset = amount >= transcript_scroll_offset ? 0 : transcript_scroll_offset - amount;
      snapshot.status =
          transcript_scroll_offset == 0 ? "transcript: latest messages" : "transcript: scrollback requested";
    };
    auto select_slash_command = [&]() {
      selected_slash_command_index =
          clamp_slash_palette_selection(snapshot.input, snapshot.slash_commands, selected_slash_command_index);
      snapshot.input =
          slash_command_selection_text(snapshot.input, snapshot.slash_commands, selected_slash_command_index);
      input_cursor = snapshot.input.size();
      selected_slash_command_index = 0;
      history_index.reset();
      draft_input.clear();
      snapshot.status = "command selected - press Enter to run";
    };
    if (event.key == Key::Character) {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      input_cursor = clamp_input_cursor(snapshot.input, input_cursor);
      const auto text = input.text.empty() ? std::string(1, event.character) : input.text;
      snapshot.input.insert(input_cursor, text);
      input_cursor += text.size();
    } else if (event.key == Key::ShiftEnter) {
      insert_newline();
    } else if (event.key == Key::Backspace) {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      erase_utf8_codepoint_before_cursor(snapshot.input, input_cursor);
    } else if (event.key == Key::Tab) {
      pending_escape_clear = false;
      if (!options.on_toggle_mode) {
        snapshot.status = "mode toggle unavailable";
      } else if (auto result = options.on_toggle_mode(); !result) {
        snapshot.status = result.error().format();
      } else {
        snapshot.mode = *result;
        snapshot.status = "mode switched to " + snapshot.mode;
      }
    } else if (event.key == Key::CtrlC) {
      break;
    } else if (event.key == Key::CtrlD) {
      break;
    } else if (event.key == Key::PageUp) {
      const auto [_, height] = terminal_size();
      scroll_up(std::max<std::size_t>(1, height / 2));
    } else if (event.key == Key::PageDown) {
      const auto [_, height] = terminal_size();
      scroll_down(std::max<std::size_t>(1, height / 2));
    } else if (event.key == Key::MouseWheelUp) {
      scroll_up(3);
    } else if (event.key == Key::MouseWheelDown) {
      scroll_down(3);
    } else if (event.key == Key::MouseLeftClick) {
      pending_escape_clear = false;
      if (const auto clicked = slash_palette_selection_for_screen_row(snapshot, event.mouse_row)) {
        selected_slash_command_index = *clicked;
        select_slash_command();
      }
    } else if (event.key == Key::ArrowLeft) {
      pending_escape_clear = false;
      input_cursor = previous_input_cursor(snapshot.input, input_cursor);
    } else if (event.key == Key::ArrowRight) {
      pending_escape_clear = false;
      input_cursor = next_input_cursor(snapshot.input, input_cursor);
    } else if (event.key == Key::ArrowUp) {
      pending_escape_clear = false;
      if (slash_palette_visible(snapshot.input, snapshot.slash_commands)) {
        const auto matches = filter_slash_commands(snapshot.input, snapshot.slash_commands);
        if (matches.empty()) {
          snapshot.status = "no matching slash commands";
        } else {
          selected_slash_command_index =
              previous_slash_palette_selection(snapshot.input, snapshot.slash_commands, selected_slash_command_index);
          snapshot.status =
              "command " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
        }
      } else if (input_history.empty()) {
        scroll_up(3);
      } else {
        if (!history_index) {
          draft_input = snapshot.input;
          history_index = input_history.size() - 1;
        } else if (*history_index > 0) {
          --(*history_index);
        }
        snapshot.input = input_history[*history_index];
        input_cursor = snapshot.input.size();
        snapshot.status = "history " + std::to_string(*history_index + 1) + "/" + std::to_string(input_history.size());
      }
    } else if (event.key == Key::ArrowDown) {
      pending_escape_clear = false;
      if (slash_palette_visible(snapshot.input, snapshot.slash_commands)) {
        const auto matches = filter_slash_commands(snapshot.input, snapshot.slash_commands);
        if (matches.empty()) {
          snapshot.status = "no matching slash commands";
        } else {
          selected_slash_command_index =
              next_slash_palette_selection(snapshot.input, snapshot.slash_commands, selected_slash_command_index);
          snapshot.status =
              "command " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
        }
      } else if (!history_index) {
        scroll_down(3);
      } else if (*history_index + 1 < input_history.size()) {
        ++(*history_index);
        snapshot.input = input_history[*history_index];
        input_cursor = snapshot.input.size();
        snapshot.status = "history " + std::to_string(*history_index + 1) + "/" + std::to_string(input_history.size());
      } else {
        history_index.reset();
        snapshot.input = draft_input;
        input_cursor = snapshot.input.size();
        draft_input.clear();
        snapshot.status = "restored current draft";
      }
    } else if (event.key == Key::Escape) {
      if (slash_palette_visible(snapshot.input, snapshot.slash_commands)) {
        pending_escape_clear = false;
        snapshot.input.clear();
        input_cursor = 0;
        selected_slash_command_index = 0;
        history_index.reset();
        draft_input.clear();
        snapshot.status = "slash palette dismissed";
      } else if (!snapshot.input.empty()) {
        if (pending_escape_clear) {
          snapshot.input.clear();
          input_cursor = 0;
          selected_slash_command_index = 0;
          history_index.reset();
          draft_input.clear();
          pending_escape_clear = false;
          snapshot.status = "input cleared";
        } else {
          pending_escape_clear = true;
          snapshot.status = "press Esc again to clear";
        }
      } else {
        pending_escape_clear = false;
        snapshot.status = "escape ignored";
      }
    } else if (event.key == Key::Enter) {
      const auto action = handle_submit();
      if (action == InputLoopAction::BreakLoop) break;
      if (action == InputLoopAction::ContinueLoop) continue;
    }
    snapshot.selected_slash_command_index = selected_slash_command_index;
    if (!render()) {
      terminal_write_failed = true;
      break;
    }
  }

  return terminal_signal_received() ? 130 : (terminal_write_failed ? 1 : 0);
}

}  // namespace ava::tui
