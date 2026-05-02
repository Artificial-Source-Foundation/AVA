#include "ava/tui/runtime.h"

#include <curses.h>

#include <algorithm>
#include <climits>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cwchar>
#include <deque>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "ava/tui/composer.h"
#include "ava/tui/composer_editor.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/terminal.h"

namespace ava::tui {
namespace {

constexpr std::size_t kMaxTranscriptScrollOffsetRequest = 10'000;
constexpr std::size_t kMaxBracketedPasteBytes = 1024 * 1024;

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
  bool bracketed_paste = false;
  bool resize = false;
};

CursesInput key_input(Key key) {
  return CursesInput{.event = InputEvent{.key = key, .character = '\0', .text = {}, .mouse_column = 0, .mouse_row = 0},
                     .text = {},
                     .bracketed_paste = false,
                     .resize = false};
}

CursesInput unknown_input() { return key_input(Key::Unknown); }

std::optional<std::string> encode_wide_character(wchar_t character) {
  std::mbstate_t state{};
  char buffer[MB_LEN_MAX]{};
  const auto length = std::wcrtomb(buffer, character, &state);
  if (length == static_cast<std::size_t>(-1)) return std::nullopt;
  return std::string(buffer, length);
}

CursesInput character_input(std::string text, bool bracketed_paste = false) {
  const auto first_byte = text.empty() ? '\0' : text[0];
  auto event_text = text;
  return CursesInput{.event = InputEvent{.key = Key::Character,
                                         .character = first_byte,
                                         .text = std::move(event_text),
                                         .mouse_column = 0,
                                         .mouse_row = 0},
                     .text = std::move(text),
                     .bracketed_paste = bracketed_paste,
                     .resize = false};
}

std::deque<std::string>& pending_character_inputs() {
  static std::deque<std::string> pending;
  return pending;
}

void push_pending_character_inputs(std::string_view text) {
  auto& pending = pending_character_inputs();
  for (const char byte : text) {
    pending.push_back(std::string(1, byte));
  }
}

std::optional<wchar_t> read_plain_wide_character() {
  wint_t value = 0;
  const auto result = wget_wch(stdscr, &value);
  if (result == ERR || result == KEY_CODE_YES) return std::nullopt;
  return static_cast<wchar_t>(value);
}

std::pair<bool, std::string> read_ascii_sequence(std::string_view expected) {
  std::string consumed;
  consumed.reserve(expected.size());
  for (const auto expected_char : expected) {
    const auto character = read_plain_wide_character();
    if (!character) return {false, consumed};
    auto encoded = encode_wide_character(*character);
    if (encoded) consumed += *encoded;
    if (*character != static_cast<unsigned char>(expected_char)) return {false, consumed};
  }
  return {true, consumed};
}

CursesInput read_bracketed_paste() {
  std::string pasted;
  static_cast<void>(wtimeout(stdscr, 1000));
  while (!terminal_signal_received() && pasted.size() < kMaxBracketedPasteBytes) {
    const auto character = read_plain_wide_character();
    if (!character) break;
    if (*character == L'\x1b') {
      auto [matched_end, consumed] = read_ascii_sequence("[201~");
      if (matched_end) break;
      pasted.push_back('\x1b');
      if (pasted.size() + consumed.size() > kMaxBracketedPasteBytes) {
        break;
      }
      pasted += consumed;
      continue;
    }
    if (auto encoded = encode_wide_character(*character)) {
      if (pasted.size() + encoded->size() > kMaxBracketedPasteBytes) {
        break;
      }
      pasted += *encoded;
    }
  }
  static_cast<void>(wtimeout(stdscr, -1));
  return character_input(normalize_composer_paste_text(pasted), true);
}

bool try_read_bracketed_paste_start() {
  static_cast<void>(wtimeout(stdscr, 50));
  auto [matched, consumed] = read_ascii_sequence("[200~");
  static_cast<void>(wtimeout(stdscr, -1));
  if (!matched) push_pending_character_inputs(consumed);
  return matched;
}

CursesInput read_curses_input() {
  auto& pending = pending_character_inputs();
  if (!pending.empty()) {
    auto text = std::move(pending.front());
    pending.pop_front();
    return character_input(std::move(text));
  }

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
        return CursesInput{
            .event = InputEvent{.key = Key::Unknown, .character = '\0', .text = {}, .mouse_column = 0, .mouse_row = 0},
            .text = {},
            .bracketed_paste = false,
            .resize = true};
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
                                                 .character = '\0',
                                                 .text = {},
                                                 .mouse_column = static_cast<std::size_t>(mouse.x + 1),
                                                 .mouse_row = static_cast<std::size_t>(mouse.y + 1)},
                             .text = {},
                             .bracketed_paste = false,
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
  if (character == 0x1B) {
    if (try_read_bracketed_paste_start()) return read_bracketed_paste();
    return key_input(Key::Escape);
  }
  if (character == 0x01) return key_input(Key::CtrlA);
  if (character == 0x02) return key_input(Key::CtrlB);
  if (character == 0x03) return key_input(Key::CtrlC);
  if (character == 0x04) return key_input(Key::CtrlD);
  if (character == 0x05) return key_input(Key::CtrlE);
  if (character == 0x06) return key_input(Key::CtrlF);
  if (character == 0x0B) return key_input(Key::CtrlK);
  if (character == 0x14) return key_input(Key::CtrlT);
  if (character == 0x15) return key_input(Key::CtrlU);
  if (character == 0x17) return key_input(Key::CtrlW);
  if (character == 0x19) return key_input(Key::CtrlY);
  if (character == 0x1A) return key_input(Key::CtrlZ);
  if (character == 0x7F || character == L'\b') return key_input(Key::Backspace);
  if (character >= 0x20) {
    auto encoded = encode_wide_character(character);
    if (!encoded) return unknown_input();
    return character_input(std::move(*encoded));
  }
  return unknown_input();
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
  if (!history.empty() && history.back() == input) return;
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

QuestionPromptView question_prompt_view(const ava::agent::QuestionPrompt& prompt) {
  QuestionPromptView view;
  view.header = prompt.header;
  view.question = prompt.question;
  view.multiple = prompt.multiple;
  view.allow_custom = prompt.allow_custom;
  view.options.reserve(prompt.options.size());
  for (const auto& option : prompt.options) {
    view.options.push_back(QuestionPromptOptionView{.value = option.value, .label = option.label, .selected = false});
  }
  return view;
}

ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_view(const QuestionPromptView& prompt) {
  ava::agent::QuestionAnswer answer;
  for (const auto& option : prompt.options) {
    if (option.selected) answer.selected_options.push_back(option.value);
  }

  if (prompt.allow_custom && !prompt.custom_text.empty()) {
    answer.custom_text = prompt.custom_text;
  }

  if (!prompt.multiple && answer.selected_options.empty() && answer.custom_text.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt resolved without an answer"));
  }

  return answer;
}

}  // namespace

ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_prompt_view(const QuestionPromptView& prompt) {
  return question_answer_from_view(prompt);
}

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
                            .status = options.initial_status,
                            .transcript = {},
                            .slash_commands = std::move(options.slash_commands)};

  bool terminal_write_failed = false;
  std::vector<std::string> input_history;
  std::optional<std::size_t> history_index;
  std::string draft_input;
  ComposerDraftState draft;
  std::size_t selected_slash_command_index = 0;
  bool slash_palette_suppressed = false;
  std::size_t transcript_scroll_offset = 0;
  bool pending_escape_clear = false;

  auto render = [&]() -> bool {
    if (terminal_signal_received()) return false;
    bool wrote = false;
    {
      SignalBlockGuard block_signals;
      draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
      snapshot.input = draft.text;
      snapshot.input_cursor = draft.cursor;
      snapshot.selected_slash_command_index = selected_slash_command_index;
      snapshot.slash_palette_suppressed = slash_palette_suppressed;
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
    static_cast<void>(insert_composer_draft_text(draft, "\n"));
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

  ava::agent::QuestionResolver question_resolver =
      [&](const ava::agent::QuestionPrompt& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
    snapshot.question_prompt = question_prompt_view(prompt);
    snapshot.status = prompt.multiple ? "question required: Space toggles, Enter sends, Esc cancels"
                                      : "question required: Enter sends, numbers choose, Esc cancels";
    static_cast<void>(beep());
    if (!render()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
    }

    auto cancel_question = [&]() -> ava::core::Result<ava::agent::QuestionAnswer> {
      snapshot.question_prompt.reset();
      snapshot.status = "question canceled";
      static_cast<void>(render());
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt canceled"));
    };

    while (true) {
      const auto question_input = read_curses_input();
      if (terminal_signal_received()) {
        snapshot.question_prompt.reset();
        snapshot.status = "interrupted";
        static_cast<void>(render());
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
      }
      if (question_input.resize) {
        if (!render()) {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
        }
        continue;
      }

      auto input_result = snapshot.question_prompt
                              ? handle_question_prompt_input(*snapshot.question_prompt, question_input.event)
                              : QuestionPromptInputResult{};
      if (input_result.action == QuestionPromptInputAction::Cancel) return cancel_question();

      if (snapshot.question_prompt && (input_result.action == QuestionPromptInputAction::Redraw ||
                                       input_result.action == QuestionPromptInputAction::Resolve)) {
        snapshot.question_prompt->selected_option_index = input_result.selected_option_index;
        snapshot.question_prompt->options = std::move(input_result.options);
        snapshot.question_prompt->custom_text = std::move(input_result.custom_text);
      }

      if (input_result.action == QuestionPromptInputAction::Resolve) {
        auto answer = ava::core::Result<ava::agent::QuestionAnswer>{
            std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt was dismissed"))};
        if (snapshot.question_prompt) answer = question_answer_from_view(*snapshot.question_prompt);
        snapshot.question_prompt.reset();
        snapshot.status = answer ? "question answered" : "question canceled";
        if (!render()) {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear question prompt"));
        }
        return answer;
      }

      if (input_result.action == QuestionPromptInputAction::Redraw) {
        if (!render()) {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
        }
        continue;
      }

      if (!render()) {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
      }
    }
  };

  enum class InputLoopAction { None, ContinueLoop, BreakLoop };
  auto handle_submit = [&]() -> InputLoopAction {
    pending_escape_clear = false;
    if (!slash_palette_suppressed && slash_palette_visible(draft.text, snapshot.slash_commands)) {
      const auto matches = filter_slash_commands(draft.text, snapshot.slash_commands);
      if (!matches.empty()) {
        selected_slash_command_index =
            clamp_slash_palette_selection(draft.text, snapshot.slash_commands, selected_slash_command_index);
        if (const auto disabled_reason = slash_command_selection_disabled_reason(draft.text, snapshot.slash_commands,
                                                                                 selected_slash_command_index)) {
          snapshot.status = "command disabled: " + *disabled_reason;
          static_cast<void>(beep());
          if (!render()) {
            terminal_write_failed = true;
            return InputLoopAction::BreakLoop;
          }
          return InputLoopAction::ContinueLoop;
        }
        static_cast<void>(replace_composer_draft(
            draft, slash_command_selection_text(draft.text, snapshot.slash_commands, selected_slash_command_index)));
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
    const auto submitted = draft.text;
    reset_composer_draft(draft);
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    if (!submitted.empty()) {
      push_transcript(snapshot, TranscriptItem{.label = submitted.starts_with('/') ? "cmd" : "you", .text = submitted});
      push_history(input_history, submitted);
      snapshot.status = submitted.starts_with('/') ? "running command..." : "thinking...";
      snapshot.processing = true;
      ++snapshot.spinner_frame;
      if (!render()) {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      const auto result =
          options.on_submit ? options.on_submit(submitted, permission_resolver, question_resolver) : TuiSubmitResult{};
      if (terminal_signal_received()) return InputLoopAction::BreakLoop;
      for (const auto& tool : result.tool_timeline) {
        push_transcript(snapshot, TranscriptItem{.tool = tool});
      }
      for (const auto& output : result.output) {
        push_transcript(snapshot, TranscriptItem{.label = "ava", .text = output});
      }
      transcript_scroll_offset = 0;
      snapshot.status = result.output.empty() ? "ok" : "done";
      snapshot.processing = false;
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
    auto is_action = [&](TuiAction action) { return key_matches_action(options.key_bindings, action, event.key); };
    auto slash_palette_active = [&]() {
      return !slash_palette_suppressed && slash_palette_visible(draft.text, snapshot.slash_commands);
    };
    auto scroll_up = [&](std::size_t amount) {
      pending_escape_clear = false;
      const auto remaining_scroll = transcript_scroll_offset >= kMaxTranscriptScrollOffsetRequest
                                        ? std::size_t{0}
                                        : kMaxTranscriptScrollOffsetRequest - transcript_scroll_offset;
      transcript_scroll_offset += std::min(amount, remaining_scroll);
      static_cast<void>(remaining_scroll);
    };
    auto scroll_down = [&](std::size_t amount) {
      pending_escape_clear = false;
      transcript_scroll_offset = amount >= transcript_scroll_offset ? 0 : transcript_scroll_offset - amount;
    };
    auto select_slash_command = [&]() {
      selected_slash_command_index =
          clamp_slash_palette_selection(draft.text, snapshot.slash_commands, selected_slash_command_index);
      if (const auto disabled_reason = slash_command_selection_disabled_reason(draft.text, snapshot.slash_commands,
                                                                               selected_slash_command_index)) {
        snapshot.status = "command disabled: " + *disabled_reason;
        static_cast<void>(beep());
        return;
      }
      static_cast<void>(replace_composer_draft(
          draft, slash_command_selection_text(draft.text, snapshot.slash_commands, selected_slash_command_index)));
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
      slash_palette_suppressed = false;
      const auto text = input.text.empty() ? std::string(1, event.character) : input.text;
      if (insert_composer_draft_text(draft, text) && input.bracketed_paste)
        snapshot.status = "pasted into draft safely";
    } else if (is_action(TuiAction::NewLine)) {
      insert_newline();
    } else if (is_action(TuiAction::DeleteBackward)) {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteBackward));
    } else if (is_action(TuiAction::DeleteWordBackward)) {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteWordBackward));
    } else if (is_action(TuiAction::DeleteToLineStart)) {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineStart));
    } else if (is_action(TuiAction::DeleteToLineEnd)) {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineEnd));
    } else if (is_action(TuiAction::ClearInput)) {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      snapshot.status =
          apply_composer_draft_action(draft, TuiAction::ClearInput) ? "input cleared" : "input already empty";
    } else if (is_action(TuiAction::AutocompleteAccept) && slash_palette_active()) {
      pending_escape_clear = false;
      select_slash_command();
    } else if (is_action(TuiAction::ModeToggle)) {
      pending_escape_clear = false;
      if (!options.on_toggle_mode) {
        snapshot.status = "mode toggle unavailable";
      } else if (auto result = options.on_toggle_mode(); !result) {
        snapshot.status = result.error().format();
      } else {
        snapshot.mode = *result;
        snapshot.status = "mode switched to " + snapshot.mode;
      }
    } else if (is_action(TuiAction::Interrupt)) {
      break;
    } else if (is_action(TuiAction::Exit)) {
      break;
    } else if (is_action(TuiAction::VariantCycle)) {
      pending_escape_clear = false;
      snapshot.status = "variant cycling is not available yet";
    } else if (is_action(TuiAction::PageUp)) {
      const auto [_, height] = terminal_size();
      scroll_up(std::max<std::size_t>(1, height / 2));
    } else if (is_action(TuiAction::PageDown)) {
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
    } else if (is_action(TuiAction::CursorLeft)) {
      pending_escape_clear = false;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLeft));
    } else if (is_action(TuiAction::CursorRight)) {
      pending_escape_clear = false;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorRight));
    } else if (is_action(TuiAction::CursorLineStart)) {
      pending_escape_clear = false;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLineStart));
    } else if (is_action(TuiAction::CursorLineEnd)) {
      pending_escape_clear = false;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLineEnd));
    } else if (is_action(TuiAction::CursorWordLeft)) {
      pending_escape_clear = false;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorWordLeft));
    } else if (is_action(TuiAction::CursorWordRight)) {
      pending_escape_clear = false;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorWordRight));
    } else if (is_action(TuiAction::PalettePrev) && slash_palette_active()) {
      pending_escape_clear = false;
      const auto matches = filter_slash_commands(draft.text, snapshot.slash_commands);
      if (matches.empty()) {
        snapshot.status = "no matching slash commands";
      } else {
        selected_slash_command_index =
            previous_slash_palette_selection(draft.text, snapshot.slash_commands, selected_slash_command_index);
        snapshot.status =
            "command " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
      }
    } else if (is_action(TuiAction::HistoryPrev)) {
      pending_escape_clear = false;
      if (input_history.empty()) {
        scroll_up(3);
      } else {
        if (!history_index) {
          draft_input = draft.text;
          history_index = input_history.size() - 1;
        } else if (*history_index > 0) {
          --(*history_index);
        }
        reset_composer_draft(draft, input_history[*history_index]);
        snapshot.status = "history " + std::to_string(*history_index + 1) + "/" + std::to_string(input_history.size());
      }
    } else if (is_action(TuiAction::PaletteNext) && slash_palette_active()) {
      pending_escape_clear = false;
      const auto matches = filter_slash_commands(draft.text, snapshot.slash_commands);
      if (matches.empty()) {
        snapshot.status = "no matching slash commands";
      } else {
        selected_slash_command_index =
            next_slash_palette_selection(draft.text, snapshot.slash_commands, selected_slash_command_index);
        snapshot.status =
            "command " + std::to_string(selected_slash_command_index + 1) + "/" + std::to_string(matches.size());
      }
    } else if (is_action(TuiAction::HistoryNext)) {
      pending_escape_clear = false;
      if (!history_index) {
        scroll_down(3);
      } else if (*history_index + 1 < input_history.size()) {
        ++(*history_index);
        reset_composer_draft(draft, input_history[*history_index]);
        snapshot.status = "history " + std::to_string(*history_index + 1) + "/" + std::to_string(input_history.size());
      } else {
        history_index.reset();
        reset_composer_draft(draft, draft_input);
        draft_input.clear();
        snapshot.status = "restored current draft";
      }
    } else if (is_action(TuiAction::Undo)) {
      pending_escape_clear = false;
      snapshot.status = apply_composer_draft_action(draft, TuiAction::Undo) ? "undo" : "nothing to undo";
    } else if (is_action(TuiAction::Yank)) {
      pending_escape_clear = false;
      snapshot.status = apply_composer_draft_action(draft, TuiAction::Yank) ? "yanked text" : "nothing to yank";
    } else if (is_action(TuiAction::DetailsToggle)) {
      pending_escape_clear = false;
      snapshot.status = "details toggle is not available yet";
    } else if (is_action(TuiAction::PromptAllow) || is_action(TuiAction::PromptDeny)) {
      pending_escape_clear = false;
      snapshot.status = "prompt action is only available while a prompt is active";
    } else if (is_action(TuiAction::Cancel)) {
      if (slash_palette_active()) {
        pending_escape_clear = false;
        slash_palette_suppressed = true;
        selected_slash_command_index = 0;
        history_index.reset();
        draft_input.clear();
        snapshot.status = "slash palette dismissed";
      } else if (!draft.text.empty()) {
        if (pending_escape_clear) {
          static_cast<void>(apply_composer_draft_action(draft, TuiAction::ClearInput));
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
    } else if (is_action(TuiAction::Submit)) {
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
