#include "ava/tui/runtime.h"

#include "ava/tui/composer.h"
#include "ava/tui/composer_editor.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/event_state.h"
#include "ava/tui/keybindings.h"
#include "ava/tui/terminal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cwchar>
#include <deque>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <curses.h>

namespace ava::tui {
namespace {

constexpr std::size_t kMaxBracketedPasteBytes = 1024 * 1024;
constexpr std::size_t kMaxEscapeSequenceBytes = 16 * 1024;
constexpr std::size_t kMaxTranscriptItems = 1000;
constexpr auto kSpinnerFrameDelay = std::chrono::milliseconds(80);

class SignalBlockGuard {
 public:
  SignalBlockGuard()
  {
    sigset_t blocked{};
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    active_ = sigprocmask(SIG_BLOCK, &blocked, &previous_) == 0;
  }

  SignalBlockGuard(SignalBlockGuard const&) = delete;
  SignalBlockGuard& operator=(SignalBlockGuard const&) = delete;

  ~SignalBlockGuard()
  {
    if (active_) static_cast<void>(sigprocmask(SIG_SETMASK, &previous_, nullptr));
  }

 private:
  sigset_t previous_{};
  bool active_ = false;
};

std::pair<std::size_t, std::size_t> terminal_size()
{
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

struct PendingPermissionRequest {
  explicit PendingPermissionRequest(ava::permissions::PermissionPrompt prompt_in) : prompt(std::move(prompt_in)) {}

  ava::permissions::PermissionPrompt prompt;
  std::mutex mutex;
  std::condition_variable ready;
  std::optional<ava::core::Result<ava::permissions::PermissionResolution>> result;
};

struct PendingQuestionRequest {
  explicit PendingQuestionRequest(ava::agent::QuestionPrompt prompt_in) : prompt(std::move(prompt_in)) {}

  ava::agent::QuestionPrompt prompt;
  std::mutex mutex;
  std::condition_variable ready;
  std::optional<ava::core::Result<ava::agent::QuestionAnswer>> result;
};

struct EventEnvelopeQueue {
  std::mutex mutex;
  std::vector<ava::app::EventEnvelope> events;
  bool received = false;

  [[nodiscard]] ava::app::EventEnvelopeSink sink()
  {
    return [this](ava::app::EventEnvelope const& event) -> ava::core::VoidResult {
      std::lock_guard<std::mutex> lock(mutex);
      events.push_back(event);
      received = true;
      return {};
    };
  }

  [[nodiscard]] std::vector<ava::app::EventEnvelope> drain()
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto drained = std::move(events);
    events.clear();
    return drained;
  }

  [[nodiscard]] bool received_any()
  {
    std::lock_guard<std::mutex> lock(mutex);
    return received;
  }
};

enum class RuntimeEventDrainResult { NoEvents, Rendered, RenderFailed };

bool mouse_state_matches(mmask_t state, mmask_t mask)
{
  return (state & mask) != 0;
}

CursesInput key_input(Key key)
{
  return CursesInput{.event = InputEvent{.key = key, .character = '\0', .text = {}, .mouse_column = 0, .mouse_row = 0},
                     .text = {},
                     .bracketed_paste = false,
                     .resize = false};
}

CursesInput mouse_key_input(Key key, const MEVENT& mouse)
{
  return CursesInput{.event = InputEvent{.key = key,
                                         .character = '\0',
                                         .text = {},
                                         .mouse_column = static_cast<std::size_t>(mouse.x + 1),
                                         .mouse_row = static_cast<std::size_t>(mouse.y + 1)},
                     .text = {},
                     .bracketed_paste = false,
                     .resize = false};
}

CursesInput unknown_input()
{
  return key_input(Key::Unknown);
}

std::string title_case_ascii(std::string_view text)
{
  std::string output;
  output.reserve(text.size());
  bool at_word_start = true;
  for (char ch : text) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte == '_' || byte == '-') {
      output.push_back(' ');
      at_word_start = true;
      continue;
    }
    if (at_word_start && byte >= 'a' && byte <= 'z') {
      output.push_back(static_cast<char>(byte - ('a' - 'A')));
    } else {
      output.push_back(ch);
    }
    at_word_start = byte == ' ';
  }
  return output;
}

std::string assistant_meta_for_snapshot(ComposerSnapshot const& snapshot)
{
  if (snapshot.model.empty()) return {};
  auto mode = title_case_ascii(snapshot.mode);
  if (mode.empty()) mode = "AVA";
  return mode + " - " + snapshot.model;
}

bool is_compact_command(std::string_view line) noexcept
{
  return line == "/compact" || (line.starts_with("/compact") && line.size() > 8 && line[8] == ' ');
}

std::size_t transcript_height_for_snapshot(ComposerSnapshot const& snapshot, std::size_t width, std::size_t height)
{
  auto const normal_composer_lines = detail::composer_block_line_count(snapshot, height);
  auto const modal_question = snapshot.question_prompt && snapshot.question_prompt->modal;
  auto const prompt_active = snapshot.permission_prompt.has_value() || (snapshot.question_prompt && !modal_question);
  auto const fixed_lines = prompt_active ? std::size_t{0} : normal_composer_lines;
  auto const max_prompt_lines = height > fixed_lines ? height - fixed_lines : 0;
  auto const prompt_line_budget = prompt_active ? std::min<std::size_t>({7, max_prompt_lines}) : 0;
  auto const permission_lines = snapshot.permission_prompt ? detail::render_permission_prompt(
                                                                 *snapshot.permission_prompt, width, prompt_line_budget)
                                                           : std::vector<std::string>{};
  auto const question_lines = snapshot.question_prompt && !modal_question
                                  ? detail::render_question_prompt(*snapshot.question_prompt, width, prompt_line_budget)
                                  : std::vector<std::string>{};
  auto const fixed_and_prompt_lines = fixed_lines + permission_lines.size() + question_lines.size();
  auto const palette_line_budget =
      (height > fixed_and_prompt_lines && !prompt_active && !snapshot.slash_palette_suppressed)
          ? std::min(detail::kMaxPaletteLines, height - fixed_and_prompt_lines)
          : 0;
  auto const palette_lines = detail::render_slash_palette(snapshot, width, palette_line_budget);
  auto const non_transcript_lines =
      fixed_lines + palette_lines.size() + permission_lines.size() + question_lines.size();
  return height > non_transcript_lines ? height - non_transcript_lines : 0;
}

std::size_t max_transcript_scroll_offset_for_snapshot(ComposerSnapshot const& snapshot, std::size_t width,
                                                      std::size_t height)
{
  auto const transcript_height = transcript_height_for_snapshot(snapshot, width, height);
  if (transcript_height == 0) return 0;
  auto const rendered_transcript = detail::render_transcript_lines(
      snapshot.transcript, width, snapshot.tool_details_visible, snapshot.thinking_visible);
  return rendered_transcript.size() > transcript_height ? rendered_transcript.size() - transcript_height : 0;
}

std::optional<std::string> encode_wide_character(wchar_t character)
{
  std::mbstate_t state{};
  char buffer[MB_LEN_MAX]{};
  auto const length = std::wcrtomb(buffer, character, &state);
  if (length == static_cast<std::size_t>(-1)) return std::nullopt;
  return std::string(buffer, length);
}

CursesInput character_input(std::string text, bool bracketed_paste = false)
{
  auto const first_byte = text.empty() ? '\0' : text[0];
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

std::optional<wchar_t> read_plain_wide_character()
{
  wint_t value = 0;
  auto const result = wget_wch(stdscr, &value);
  if (result == ERR || result == KEY_CODE_YES) return std::nullopt;
  return static_cast<wchar_t>(value);
}

std::pair<bool, std::string> read_ascii_sequence(std::string_view expected)
{
  std::string consumed;
  consumed.reserve(expected.size());
  for (auto const expected_char : expected) {
    auto const character = read_plain_wide_character();
    if (!character) return {false, consumed};
    auto encoded = encode_wide_character(*character);
    if (encoded) consumed += *encoded;
    if (*character != static_cast<unsigned char>(expected_char)) return {false, consumed};
  }
  return {true, consumed};
}

CursesInput read_bracketed_paste()
{
  std::string pasted;
  static_cast<void>(wtimeout(stdscr, 1000));
  while (!terminal_signal_received() && pasted.size() < kMaxBracketedPasteBytes) {
    auto const character = read_plain_wide_character();
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

std::optional<CursesInput> read_escape_sequence_input()
{
  static_cast<void>(wtimeout(stdscr, 50));
  std::string consumed;
  consumed.reserve(32);
  while (consumed.size() < kMaxEscapeSequenceBytes) {
    auto const character = read_plain_wide_character();
    if (!character) break;
    if (auto encoded = encode_wide_character(*character)) consumed += *encoded;
    if (terminal_escape_sequence_complete(consumed)) break;
  }
  static_cast<void>(wtimeout(stdscr, -1));

  if (consumed.empty()) return std::nullopt;
  if (consumed == "[200~") return read_bracketed_paste();
  if (auto const key = terminal_escape_sequence_key(consumed); key != Key::Unknown) return key_input(key);
  if (terminal_escape_sequence_should_discard(consumed) || !terminal_escape_sequence_complete(consumed)) {
    return unknown_input();
  }
  return unknown_input();
}

CursesInput read_curses_input()
{
  wint_t value = 0;
  auto const result = wget_wch(stdscr, &value);
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
        if (mouse_state_matches(mouse.bstate, BUTTON4_PRESSED)
#ifdef BUTTON4_CLICKED
            || mouse_state_matches(mouse.bstate, BUTTON4_CLICKED)
#endif
#ifdef BUTTON4_RELEASED
            || mouse_state_matches(mouse.bstate, BUTTON4_RELEASED)
#endif
        ) {
          return mouse_key_input(Key::MouseWheelUp, mouse);
        }
        if (mouse_state_matches(mouse.bstate, BUTTON5_PRESSED)
#ifdef BUTTON5_CLICKED
            || mouse_state_matches(mouse.bstate, BUTTON5_CLICKED)
#endif
#ifdef BUTTON5_RELEASED
            || mouse_state_matches(mouse.bstate, BUTTON5_RELEASED)
#endif
        ) {
          return mouse_key_input(Key::MouseWheelDown, mouse);
        }
        if ((mouse.bstate & BUTTON1_CLICKED) != 0) {
          return mouse_key_input(Key::MouseLeftClick, mouse);
        }
        return unknown_input();
      }
#endif
      default:
        return unknown_input();
    }
  }

  auto const character = static_cast<wchar_t>(value);
  if (character == L'\r') return key_input(Key::Enter);
  if (character == L'\n') return key_input(Key::ShiftEnter);
  if (character == L'\t') return key_input(Key::Tab);
  if (character == 0x1B) {
    if (auto escape_input = read_escape_sequence_input()) return *escape_input;
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

std::optional<CursesInput> poll_curses_input()
{
  static_cast<void>(wtimeout(stdscr, 0));
  auto input = read_curses_input();
  static_cast<void>(wtimeout(stdscr, -1));
  if (!input.resize && input.event.key == Key::Unknown && input.text.empty() && !input.bracketed_paste) {
    return std::nullopt;
  }
  return input;
}

void truncate_transcript(std::vector<TranscriptItem>& transcript)
{
  if (transcript.size() > kMaxTranscriptItems) {
    transcript.erase(transcript.begin(),
                     transcript.begin() + static_cast<std::ptrdiff_t>(transcript.size() - kMaxTranscriptItems));
  }
}

void push_transcript(ComposerSnapshot& snapshot, TranscriptItem item)
{
  snapshot.transcript.push_back(std::move(item));
  truncate_transcript(snapshot.transcript);
}

void push_history(std::vector<std::string>& history, std::string input)
{
  constexpr std::size_t kMaxHistoryItems = 100;
  if (input.empty()) return;
  if (!history.empty() && history.back() == input) return;
  history.push_back(std::move(input));
  if (history.size() > kMaxHistoryItems) {
    history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(history.size() - kMaxHistoryItems));
  }
}

PermissionPromptView permission_prompt_view(ava::permissions::PermissionPrompt const& prompt)
{
  PermissionPromptView view;
  view.tool_name = prompt.tool_name;
  view.operation = ava::permissions::to_string(prompt.operation);
  view.target = prompt.target_path.empty() ? std::string{} : prompt.target_path.generic_string();
  view.command = prompt.command;
  view.reason = prompt.reason;
  view.diff_preview = prompt.diff_preview;
  view.diff_truncated = prompt.diff_truncated;
  return view;
}

QuestionPromptView question_prompt_view(ava::agent::QuestionPrompt const& prompt)
{
  QuestionPromptView view;
  view.header = prompt.header;
  view.question = prompt.question;
  view.multiple = prompt.multiple;
  view.allow_custom = prompt.allow_custom;
  view.secret = prompt.secret;
  view.modal = prompt.modal;
  view.searchable = prompt.searchable;
  view.options.reserve(prompt.options.size());
  for (auto const& option : prompt.options) {
    view.options.push_back(QuestionPromptOptionView{.value = option.value, .label = option.label, .selected = false});
  }
  return view;
}

ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_view(QuestionPromptView const& prompt)
{
  ava::agent::QuestionAnswer answer;
  for (auto const& option : prompt.options) {
    if (option.selected) answer.selected_options.push_back(option.value);
  }

  if (prompt.allow_custom && !prompt.custom_text.empty() && (!prompt.searchable || answer.selected_options.empty())) {
    answer.custom_text = prompt.custom_text;
  }

  if (!prompt.multiple && answer.selected_options.empty() && answer.custom_text.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt resolved without an answer"));
  }

  return answer;
}

}  // namespace

ava::core::Result<ava::agent::QuestionAnswer> question_answer_from_prompt_view(QuestionPromptView const& prompt)
{
  return question_answer_from_view(prompt);
}

int run_interactive_composer(TuiRuntimeOptions options)
{
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
  SidebarSnapshot sidebar{.session_id = options.session_id,
                          .mode = options.mode,
                          .provider = options.provider,
                          .model = options.model,
                          .workspace = options.workspace,
                          .git_branch = options.git_branch,
                          .version = options.app_version,
                          .context_source_count = options.context_source_count};
  auto refresh_token_status = [&]() {
    snapshot.token_status = options.token_status_provider ? options.token_status_provider() : std::nullopt;
    sidebar.token_status = snapshot.token_status;
  };
  auto refresh_reasoning_status = [&]() {
    snapshot.reasoning_status = options.reasoning_status_provider ? options.reasoning_status_provider() : std::nullopt;
    sidebar.reasoning_status = snapshot.reasoning_status;
  };
  refresh_token_status();
  refresh_reasoning_status();

  bool terminal_write_failed = false;
  std::vector<std::string> input_history;
  std::optional<std::size_t> history_index;
  std::string draft_input;
  ComposerDraftState draft;
  std::size_t selected_slash_command_index = 0;
  bool slash_palette_suppressed = false;
  std::size_t transcript_scroll_offset = 0;
  std::size_t draft_scroll_offset = 0;
  bool pending_escape_clear = false;
  std::recursive_mutex ui_mutex;
  std::mutex prompt_request_mutex;
  std::deque<std::shared_ptr<PendingPermissionRequest>> pending_permission_requests;
  std::deque<std::shared_ptr<PendingQuestionRequest>> pending_question_requests;
  std::atomic_bool accept_prompt_requests{true};
  std::mutex prompt_audit_mutex;
  ava::app::RuntimeEventSink prompt_audit_sink;

  auto emit_prompt_audit = [&](std::string status, std::string text) {
    ava::app::RuntimeEventSink sink;
    {
      std::lock_guard<std::mutex> lock(prompt_audit_mutex);
      sink = prompt_audit_sink;
    }
    if (!sink) return;
    ava::app::RuntimeEvent event;
    event.type = ava::app::RuntimeEventType::ProviderEvent;
    event.status = std::move(status);
    event.text = std::move(text);
    static_cast<void>(sink(event));
  };

  auto max_draft_scroll_offset = [&](std::size_t height) {
    auto draft_snapshot = snapshot;
    draft_snapshot.input = draft.text;
    auto const composer_lines = detail::composer_block_line_count(draft_snapshot, height);
    auto const input_lines = detail::input_render_lines(draft.text).size();
    auto const layout = detail::composer_input_layout(input_lines, composer_lines, 0);
    return input_lines > layout.visible_input_lines ? input_lines - layout.visible_input_lines : std::size_t{0};
  };

  auto render = [&]() -> bool {
    if (terminal_signal_received()) return false;
    bool wrote = false;
    {
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      SignalBlockGuard block_signals;
      draft.cursor = clamp_composer_draft_cursor(draft.text, draft.cursor);
      snapshot.input = draft.text;
      snapshot.input_cursor = draft.cursor;
      snapshot.selected_slash_command_index = selected_slash_command_index;
      snapshot.slash_palette_suppressed = slash_palette_suppressed;
      snapshot.sidebar = sidebar;
      auto const [width, height] = terminal_size();
      snapshot.width = width;
      snapshot.height = height;
      draft_scroll_offset = std::min(draft_scroll_offset, max_draft_scroll_offset(height));
      snapshot.draft_scroll_offset = draft_scroll_offset;
      auto const main_width = composer_main_width(snapshot);
      transcript_scroll_offset =
          std::min(transcript_scroll_offset, max_transcript_scroll_offset_for_snapshot(snapshot, main_width, height));
      snapshot.transcript_scroll_offset = transcript_scroll_offset;
      wrote = draw_screen(snapshot);
    }
    return wrote && !terminal_signal_received();
  };

  auto render_processing_frame = [&]() -> bool {
    {
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      if (snapshot.permission_prompt || snapshot.question_prompt) return true;
      ++snapshot.spinner_frame;
    }
    return render();
  };
  auto insert_newline = [&]() {
    pending_escape_clear = false;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    draft_scroll_offset = 0;
    static_cast<void>(insert_composer_draft_text(draft, "\n"));
  };

  auto resolve_permission_prompt =
      [&](ava::permissions::PermissionPrompt const& prompt, std::function<bool()> const& stop_requested = {},
          std::function<bool()> const& request_stop = {}) -> ava::core::Result<ava::permissions::PermissionResolution> {
    auto permission_label = std::string("permission requested");
    if (!prompt.tool_name.empty()) permission_label += ": " + prompt.tool_name;
    if (!prompt.command.empty()) permission_label += " " + prompt.command;
    if (prompt.target_path.has_filename()) permission_label += " " + prompt.target_path.generic_string();
    emit_prompt_audit("tui:permission_request", std::move(permission_label));
    {
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      snapshot.permission_prompt = permission_prompt_view(prompt);
      snapshot.permission_prompt->selected_choice = PermissionPromptChoice::Deny;
      snapshot.status = "permission required: A=allow D=deny Tab/Left/Right choose Enter/Space confirm Esc deny";
    }
    static_cast<void>(beep());
    if (!render()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render permission prompt"));
    }

    auto resolve_choice =
        [&](PermissionPromptChoice selected) -> ava::core::Result<ava::permissions::PermissionResolution> {
      if (selected == PermissionPromptChoice::Allow) {
        emit_prompt_audit("tui:permission_allow", "permission allowed: " + prompt.tool_name);
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.permission_prompt.reset();
          snapshot.status = "permission allowed once";
        }
        if (!render()) {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
        }
        return ava::permissions::PermissionResolution::Allow;
      }
      emit_prompt_audit("tui:permission_deny", "permission denied: " + prompt.tool_name);
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.permission_prompt.reset();
        snapshot.status = "permission denied";
      }
      if (!render()) {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear permission prompt"));
      }
      return ava::permissions::PermissionResolution::Deny;
    };

    while (true) {
      auto const choice_input = read_curses_input();
      if (stop_requested && stop_requested()) {
        return resolve_choice(PermissionPromptChoice::Deny);
      }
      if (terminal_signal_received()) {
        emit_prompt_audit("tui:permission_deny", "permission denied: interrupted");
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.permission_prompt.reset();
          snapshot.status = "interrupted";
        }
        static_cast<void>(render());
        return ava::permissions::PermissionResolution::Deny;
      }
      if (choice_input.resize) {
        if (!render()) {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render permission prompt"));
        }
        continue;
      }

      if (choice_input.event.key == Key::Escape && request_stop) {
        static_cast<void>(request_stop());
        return resolve_choice(PermissionPromptChoice::Deny);
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
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          if (snapshot.permission_prompt) snapshot.permission_prompt->selected_choice = input_result.selected_choice;
        }
        static_cast<void>(render());
        continue;
      }

      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.status = "permission required: A=allow D=deny Tab/Left/Right choose Enter/Space confirm Esc deny";
      }
      if (!render()) {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render permission prompt"));
      }
    }
  };

  auto resolve_question_prompt =
      [&](ava::agent::QuestionPrompt const& prompt, std::function<bool()> const& stop_requested = {},
          std::function<bool()> const& request_stop = {}) -> ava::core::Result<ava::agent::QuestionAnswer> {
    emit_prompt_audit("tui:question_request", prompt.question.empty() ? std::string("question requested")
                                                                      : "question requested: " + prompt.question);
    {
      std::lock_guard<std::recursive_mutex> lock(ui_mutex);
      snapshot.question_prompt = question_prompt_view(prompt);
      snapshot.status = prompt.multiple ? "question required: Space toggles, Enter sends, Esc cancels"
                                        : "question required: Enter sends, numbers choose, Esc cancels";
    }
    static_cast<void>(beep());
    if (!render()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
    }

    auto cancel_question = [&]() -> ava::core::Result<ava::agent::QuestionAnswer> {
      emit_prompt_audit("tui:question_cancel", prompt.question.empty() ? std::string("question canceled")
                                                                       : "question canceled: " + prompt.question);
      {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        snapshot.question_prompt.reset();
        snapshot.status = "question canceled";
      }
      static_cast<void>(render());
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt canceled"));
    };

    while (true) {
      auto const question_input = read_curses_input();
      if (stop_requested && stop_requested()) return cancel_question();
      if (terminal_signal_received()) {
        emit_prompt_audit("tui:question_cancel", "question canceled: interrupted");
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.question_prompt.reset();
          snapshot.status = "interrupted";
        }
        static_cast<void>(render());
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
      }
      if (question_input.resize) {
        if (!render()) {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to render question prompt"));
        }
        continue;
      }

      if (question_input.event.key == Key::Escape && request_stop) {
        static_cast<void>(request_stop());
        return cancel_question();
      }

      auto input_result = snapshot.question_prompt
                              ? handle_question_prompt_input(*snapshot.question_prompt, question_input.event)
                              : QuestionPromptInputResult{};
      if (input_result.action == QuestionPromptInputAction::Cancel) return cancel_question();

      if (snapshot.question_prompt && (input_result.action == QuestionPromptInputAction::Redraw ||
                                       input_result.action == QuestionPromptInputAction::Resolve)) {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        if (snapshot.question_prompt) {
          snapshot.question_prompt->selected_option_index = input_result.selected_option_index;
          snapshot.question_prompt->options = std::move(input_result.options);
          snapshot.question_prompt->custom_text = std::move(input_result.custom_text);
        }
      }

      if (input_result.action == QuestionPromptInputAction::Resolve) {
        auto answer = ava::core::Result<ava::agent::QuestionAnswer>{
            std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt was dismissed"))};
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          if (snapshot.question_prompt) answer = question_answer_from_view(*snapshot.question_prompt);
          snapshot.question_prompt.reset();
          snapshot.status = answer ? "question answered" : "question canceled";
        }
        if (!render()) {
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to clear question prompt"));
        }
        if (answer) {
          auto detail = std::string("question answered");
          if (!answer->selected_options.empty()) detail += ": " + answer->selected_options.front();
          if (!answer->custom_text.empty()) detail += answer->selected_options.empty() ? ": custom" : ", custom";
          emit_prompt_audit("tui:question_answer", std::move(detail));
        } else {
          emit_prompt_audit("tui:question_cancel", "question canceled");
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

  auto complete_permission_request = [](std::shared_ptr<PendingPermissionRequest> const& request,
                                        ava::core::Result<ava::permissions::PermissionResolution> result) {
    {
      std::lock_guard<std::mutex> lock(request->mutex);
      request->result = std::move(result);
    }
    request->ready.notify_one();
  };

  auto complete_question_request = [](std::shared_ptr<PendingQuestionRequest> const& request,
                                      ava::core::Result<ava::agent::QuestionAnswer> result) {
    {
      std::lock_guard<std::mutex> lock(request->mutex);
      request->result = std::move(result);
    }
    request->ready.notify_one();
  };

  auto fail_pending_prompt_requests = [&]() {
    accept_prompt_requests.store(false);
    std::deque<std::shared_ptr<PendingPermissionRequest>> permission_requests;
    std::deque<std::shared_ptr<PendingQuestionRequest>> question_requests;
    {
      std::lock_guard<std::mutex> lock(prompt_request_mutex);
      permission_requests = std::move(pending_permission_requests);
      question_requests = std::move(pending_question_requests);
      pending_permission_requests.clear();
      pending_question_requests.clear();
    }
    for (auto const& permission_request : permission_requests) {
      complete_permission_request(permission_request, ava::permissions::PermissionResolution::Deny);
    }
    for (auto const& question_request : question_requests) {
      complete_question_request(question_request, std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool,
                                                                                   "question prompt interrupted")));
    }
  };

  auto service_pending_prompt_request = [&](std::function<bool()> const& stop_requested = {},
                                            std::function<bool()> const& request_stop = {}) -> bool {
    std::shared_ptr<PendingPermissionRequest> permission_request;
    std::shared_ptr<PendingQuestionRequest> question_request;
    {
      std::lock_guard<std::mutex> lock(prompt_request_mutex);
      if (!pending_permission_requests.empty()) {
        permission_request = std::move(pending_permission_requests.front());
        pending_permission_requests.pop_front();
      } else if (!pending_question_requests.empty()) {
        question_request = std::move(pending_question_requests.front());
        pending_question_requests.pop_front();
      }
    }
    if (permission_request) {
      complete_permission_request(permission_request,
                                  resolve_permission_prompt(permission_request->prompt, stop_requested, request_stop));
      return true;
    }
    if (question_request) {
      complete_question_request(question_request,
                                resolve_question_prompt(question_request->prompt, stop_requested, request_stop));
      return true;
    }
    return false;
  };

  ava::permissions::PermissionResolver permission_resolver = [&](ava::permissions::PermissionPrompt const& prompt)
      -> ava::core::Result<ava::permissions::PermissionResolution> {
    if (!accept_prompt_requests.load()) return ava::permissions::PermissionResolution::Deny;
    auto request = std::make_shared<PendingPermissionRequest>(prompt);
    {
      std::lock_guard<std::mutex> lock(prompt_request_mutex);
      if (!accept_prompt_requests.load()) return ava::permissions::PermissionResolution::Deny;
      pending_permission_requests.push_back(request);
    }
    std::unique_lock<std::mutex> lock(request->mutex);
    request->ready.wait(lock, [&]() { return request->result.has_value() || !accept_prompt_requests.load(); });
    if (!request->result) return ava::permissions::PermissionResolution::Deny;
    return std::move(*request->result);
  };

  ava::agent::QuestionResolver question_resolver =
      [&](ava::agent::QuestionPrompt const& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
    if (!accept_prompt_requests.load()) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
    }
    auto request = std::make_shared<PendingQuestionRequest>(prompt);
    {
      std::lock_guard<std::mutex> lock(prompt_request_mutex);
      if (!accept_prompt_requests.load()) {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
      }
      pending_question_requests.push_back(request);
    }
    std::unique_lock<std::mutex> lock(request->mutex);
    request->ready.wait(lock, [&]() { return request->result.has_value() || !accept_prompt_requests.load(); });
    if (!request->result) {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question prompt interrupted"));
    }
    return std::move(*request->result);
  };

  auto clear_draft_for_interrupt = [&]() {
    pending_escape_clear = false;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    slash_palette_suppressed = false;
    draft_scroll_offset = 0;
    snapshot.status.clear();
    return apply_composer_draft_action(draft, TuiAction::ClearInput);
  };

  auto cycle_reasoning = [&]() {
    if (!options.on_cycle_reasoning) {
      snapshot.status = "reasoning cycling unavailable";
      return;
    }
    auto result = options.on_cycle_reasoning();
    if (!result) {
      snapshot.status = result.error().format();
      return;
    }
    snapshot.status = *result;
    refresh_reasoning_status();
  };

  auto slash_palette_active = [&]() {
    return !slash_palette_suppressed && slash_palette_visible(draft.text, snapshot.slash_commands);
  };

  auto scroll_up = [&](std::size_t amount) {
    pending_escape_clear = false;
    auto const [width, height] = terminal_size();
    snapshot.width = width;
    snapshot.height = height;
    auto const max_scroll = max_transcript_scroll_offset_for_snapshot(snapshot, composer_main_width(snapshot), height);
    auto const clamped_scroll = std::min(transcript_scroll_offset, max_scroll);
    transcript_scroll_offset = std::min(max_scroll, clamped_scroll + amount);
  };

  auto scroll_down = [&](std::size_t amount) {
    pending_escape_clear = false;
    auto const [width, height] = terminal_size();
    snapshot.width = width;
    snapshot.height = height;
    auto const max_scroll = max_transcript_scroll_offset_for_snapshot(snapshot, composer_main_width(snapshot), height);
    auto const clamped_scroll = std::min(transcript_scroll_offset, max_scroll);
    transcript_scroll_offset = amount >= clamped_scroll ? 0 : clamped_scroll - amount;
  };

  enum class InputLoopAction { None, ContinueLoop, BreakLoop };
  auto handle_submit = [&]() -> InputLoopAction {
    pending_escape_clear = false;
    if (!slash_palette_suppressed && slash_palette_visible(draft.text, snapshot.slash_commands)) {
      auto const matches = filter_slash_commands(draft.text, snapshot.slash_commands);
      if (!matches.empty()) {
        selected_slash_command_index =
            clamp_slash_palette_selection(draft.text, snapshot.slash_commands, selected_slash_command_index);
        if (auto const disabled_reason = slash_command_selection_disabled_reason(draft.text, snapshot.slash_commands,
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
        draft_scroll_offset = 0;
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
    auto const submitted = draft.text;
    reset_composer_draft(draft);
    draft_scroll_offset = 0;
    history_index.reset();
    draft_input.clear();
    selected_slash_command_index = 0;
    if (!submitted.empty()) {
      if (submitted == "/details") {
        push_history(input_history, submitted);
        snapshot.tool_details_visible = !snapshot.tool_details_visible;
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        push_transcript(snapshot, TranscriptItem{.label = "ava",
                                                 .text = snapshot.tool_details_visible ? "tool details are now visible"
                                                                                       : "tool details are now compact",
                                                 .meta = assistant_meta_for_snapshot(snapshot)});
        snapshot.status = snapshot.tool_details_visible ? "tool details visible" : "tool details compact";
        transcript_scroll_offset = 0;
        if (!render()) {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      if (submitted == "/thinking") {
        push_history(input_history, submitted);
        snapshot.thinking_visible = !snapshot.thinking_visible;
        push_transcript(snapshot, TranscriptItem{.label = "cmd", .text = submitted});
        push_transcript(snapshot, TranscriptItem{.label = "ava",
                                                 .text = snapshot.thinking_visible ? "thinking blocks are now visible"
                                                                                   : "thinking blocks are now hidden",
                                                 .meta = assistant_meta_for_snapshot(snapshot)});
        snapshot.status = snapshot.thinking_visible ? "thinking visible" : "thinking hidden";
        transcript_scroll_offset = 0;
        if (!render()) {
          terminal_write_failed = true;
          return InputLoopAction::BreakLoop;
        }
        return InputLoopAction::ContinueLoop;
      }
      auto const is_slash_command = submitted.starts_with('/');
      auto const supports_active_queue = !is_slash_command || is_compact_command(submitted);
      auto const transcript_before_submit = snapshot.transcript;
      auto submitted_transcript = transcript_before_submit;
      ava::app::EventBus event_bus;
      EventEnvelopeQueue event_queue;
      TuiEventState event_state;
      std::atomic_bool run_cancel_requested{false};
      bool close_after_submit = false;
      auto cancel_requested = [&run_cancel_requested]() { return run_cancel_requested.load(); };
      event_bus.subscribe(event_queue.sink());
      std::optional<TuiActiveRunQueues> active_queues;
      std::mutex event_context_mutex;
      std::string current_request_id;
      auto set_current_request_id = [&](std::string request_id) {
        std::lock_guard lock(event_context_mutex);
        current_request_id = std::move(request_id);
      };
      if (supports_active_queue && options.create_active_run_queues) {
        active_queues = options.create_active_run_queues(
            [&event_bus](ava::app::EventEnvelope const& event) { return event_bus.publish(event); });
        if (!active_queues->active_request_id.empty()) {
          set_current_request_id(active_queues->active_request_id);
          auto mark_follow_up_started = active_queues->mark_follow_up_started;
          active_queues->mark_follow_up_started = [&, mark_follow_up_started](TuiQueuedFollowUp const& follow_up) {
            set_current_request_id(follow_up.request_id);
            if (mark_follow_up_started) return mark_follow_up_started(follow_up);
            return ava::core::VoidResult{};
          };
        }
      }
      auto runtime_event_to_bus_sink = [&]() -> ava::app::RuntimeEventSink {
        return [&](ava::app::RuntimeEvent const& event) {
          ava::app::EventEnvelopeContext event_context;
          {
            std::lock_guard lock(event_context_mutex);
            if (!current_request_id.empty()) {
              event_context.request_id = current_request_id;
              event_context.correlation_id = current_request_id;
            }
          }
          return event_bus.publish(ava::app::to_event_envelope(event, event_context));
        };
      };
      auto event_sink = supports_active_queue ? runtime_event_to_bus_sink() : ava::app::RuntimeEventSink{};
      {
        std::lock_guard<std::mutex> lock(prompt_audit_mutex);
        prompt_audit_sink = event_sink;
      }
      auto upsert_stopping_activity = [&]() {
        auto item = SidebarActivityItem{.id = "stopping",
                                        .label = "stopping",
                                        .detail = "waiting for active work to stop",
                                        .status = ToolTimelineStatus::Running};
        auto existing = std::ranges::find_if(
            sidebar.activity, [&](SidebarActivityItem const& activity) { return activity.id == item.id; });
        if (existing == sidebar.activity.end()) {
          sidebar.activity.push_back(std::move(item));
        } else {
          *existing = std::move(item);
        }
      };
      auto settle_turn_activity = [&]() {
        auto responding = std::ranges::find_if(
            sidebar.activity, [](SidebarActivityItem const& activity) { return activity.id == "responding"; });
        if (responding == sidebar.activity.end() || responding->status != ToolTimelineStatus::Running) return;
        if (run_cancel_requested.load() || event_state.run_status == TuiEventRunStatus::Canceled) {
          responding->status = ToolTimelineStatus::Error;
          responding->detail = "assistant stopped";
          return;
        }
        if (event_state.run_status == TuiEventRunStatus::Error) {
          responding->status = ToolTimelineStatus::Error;
          responding->detail = "assistant failed";
          return;
        }
        responding->status = ToolTimelineStatus::Success;
        responding->detail = "assistant responded";
      };
      auto request_stop = [&]() -> bool {
        bool const was_already_requested = run_cancel_requested.exchange(true);
        fail_pending_prompt_requests();
        if (!was_already_requested) static_cast<void>(beep());
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.status = "stop requested";
          upsert_stopping_activity();
        }
        return render();
      };
      auto request_close_after_submit = [&]() {
        run_cancel_requested.store(true);
        close_after_submit = true;
        fail_pending_prompt_requests();
      };
      auto drain_runtime_events = [&]() -> RuntimeEventDrainResult {
        auto events = event_queue.drain();
        if (events.empty()) return RuntimeEventDrainResult::NoEvents;
        for (auto const& event : events) {
          apply_event_envelope(event_state, event);
        }
        auto const turn_transcript = event_state_transcript_snapshot(event_state);
        {
          std::lock_guard<std::recursive_mutex> lock(ui_mutex);
          snapshot.transcript = submitted_transcript;
          snapshot.transcript.insert(snapshot.transcript.end(), turn_transcript.begin(), turn_transcript.end());
          truncate_transcript(snapshot.transcript);
          snapshot.queued_messages = event_state.queued_messages;
          sidebar.activity = event_state.activity;
          if (run_cancel_requested.load() && event_state.run_status == TuiEventRunStatus::Running) {
            upsert_stopping_activity();
          }
          for (auto const& file : event_state.modified_files) {
            auto const exists = std::ranges::any_of(sidebar.modified_files, [&](SidebarModifiedFile const& existing) {
              return existing.path == file.path;
            });
            if (!exists) sidebar.modified_files.push_back(file);
          }
          constexpr auto kMaxSidebarModifiedFiles = std::size_t{50};
          if (sidebar.modified_files.size() > kMaxSidebarModifiedFiles) {
            sidebar.modified_files.erase(
                sidebar.modified_files.begin(),
                sidebar.modified_files.begin() +
                    static_cast<std::ptrdiff_t>(sidebar.modified_files.size() - kMaxSidebarModifiedFiles));
          }
          transcript_scroll_offset = 0;
        }
        return render() ? RuntimeEventDrainResult::Rendered : RuntimeEventDrainResult::RenderFailed;
      };

      if (is_slash_command) {
        auto command_item = TranscriptItem{.label = "cmd", .text = submitted};
        submitted_transcript.push_back(command_item);
        push_transcript(snapshot, std::move(command_item));
      }
      push_history(input_history, submitted);
      snapshot.status = is_slash_command ? "running command..." : "thinking...";
      snapshot.processing = true;
      if (!render()) {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      auto result = TuiSubmitResult{};
      if (options.on_submit) {
        accept_prompt_requests.store(true);
        auto submit_future = std::async(std::launch::async, [&]() {
          auto take_steering_messages = active_queues ? active_queues->take_steering_messages
                                                      : std::function<ava::core::Result<std::vector<std::string>>()>{};
          auto skip_active_steering = active_queues ? active_queues->skip_active_steering
                                                    : std::function<ava::core::VoidResult(std::string_view)>{};
          auto take_next_follow_up =
              active_queues ? active_queues->take_next_follow_up : std::function<std::optional<TuiQueuedFollowUp>()>{};
          auto mark_follow_up_started = active_queues
                                            ? active_queues->mark_follow_up_started
                                            : std::function<ava::core::VoidResult(TuiQueuedFollowUp const&)>{};
          return options.on_submit(submitted, TuiSubmitContext{.permission_resolver = permission_resolver,
                                                               .question_resolver = question_resolver,
                                                               .event_sink = event_sink,
                                                               .cancel_requested = cancel_requested,
                                                               .take_steering_messages = take_steering_messages,
                                                               .skip_active_steering = skip_active_steering,
                                                               .take_next_follow_up = take_next_follow_up,
                                                               .mark_follow_up_started = mark_follow_up_started});
        });
        auto handle_active_input = [&](CursesInput const& active_input) -> bool {
          if (active_input.resize) return render();

          auto const active_event = active_input.event;
          auto active_is_action = [&](TuiAction action) {
            return key_matches_action(options.key_bindings, action, active_event.key);
          };
          if (active_event.key == Key::Escape || active_is_action(TuiAction::Cancel)) {
            return request_stop();
          }
          if (active_is_action(TuiAction::Interrupt)) {
            if (!draft.text.empty()) {
              static_cast<void>(clear_draft_for_interrupt());
              snapshot.selected_slash_command_index = selected_slash_command_index;
              return render();
            }
            request_close_after_submit();
            return true;
          }
          if (active_is_action(TuiAction::Exit)) {
            request_close_after_submit();
            return true;
          }
          if (active_event.key == Key::Character) {
            pending_escape_clear = false;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            draft_scroll_offset = 0;
            auto const text = active_input.text.empty() ? std::string(1, active_event.character) : active_input.text;
            static_cast<void>(insert_composer_draft_text(draft, text));
            return render();
          }
          if (active_is_action(TuiAction::NewLine)) {
            insert_newline();
            return render();
          }
          if (active_is_action(TuiAction::Submit)) {
            if (draft.text.empty()) {
              snapshot.status = "type a follow-up before queueing";
              return render();
            }
            if (draft.text == "/restore") {
              if (!active_queues || !active_queues->restore_latest) {
                snapshot.status = "active-run restore unavailable";
                return render();
              }
              auto restored = active_queues->restore_latest();
              if (!restored) {
                snapshot.status = restored.error().format();
                static_cast<void>(beep());
                return render();
              }
              auto const restored_text = restored->steering ? "/steer " + restored->message : restored->message;
              static_cast<void>(replace_composer_draft(draft, restored_text));
              draft_scroll_offset = 0;
              history_index.reset();
              draft_input.clear();
              selected_slash_command_index = 0;
              slash_palette_suppressed = false;
              snapshot.status = restored->steering ? "steering restored" : "follow-up restored";
              return render();
            }
            auto const steering_prefix = std::string_view("/steer ");
            bool const steering_draft = draft.text.starts_with(steering_prefix);
            if (draft.text.starts_with('/') && !steering_draft) {
              snapshot.status = "slash commands run between turns";
              return render();
            }
            if (run_cancel_requested.load()) {
              snapshot.status = "stop requested; queueing disabled";
              return render();
            }
            if (!active_queues) {
              snapshot.status = "active-run queue unavailable";
              return render();
            }
            if (steering_draft && !active_queues->queue_steering) {
              snapshot.status = "active-run steering unavailable";
              return render();
            }
            if (!steering_draft && !active_queues->queue_follow_up) {
              snapshot.status = "active-run follow-up unavailable";
              return render();
            }

            auto queued_text = draft.text;
            auto queued = steering_draft ? active_queues->queue_steering(queued_text.substr(steering_prefix.size()))
                                         : active_queues->queue_follow_up(queued_text);
            if (!queued) {
              snapshot.status = queued.error().format();
              static_cast<void>(beep());
              return render();
            }
            push_history(input_history, queued_text);
            reset_composer_draft(draft);
            draft_scroll_offset = 0;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            snapshot.status = steering_draft ? "steering queued" : "follow-up queued";
            return render();
          }
          if (active_is_action(TuiAction::DeleteBackward) || active_is_action(TuiAction::DeleteWordBackward) ||
              active_is_action(TuiAction::DeleteToLineStart) || active_is_action(TuiAction::DeleteToLineEnd) ||
              active_is_action(TuiAction::ClearInput) || active_is_action(TuiAction::CursorLeft) ||
              active_is_action(TuiAction::CursorRight) || active_is_action(TuiAction::CursorLineStart) ||
              active_is_action(TuiAction::CursorLineEnd) || active_is_action(TuiAction::CursorWordLeft) ||
              active_is_action(TuiAction::CursorWordRight) || active_is_action(TuiAction::Undo) ||
              active_is_action(TuiAction::Yank)) {
            pending_escape_clear = false;
            history_index.reset();
            draft_input.clear();
            selected_slash_command_index = 0;
            slash_palette_suppressed = false;
            draft_scroll_offset = 0;
            if (active_is_action(TuiAction::DeleteBackward)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteBackward));
            } else if (active_is_action(TuiAction::DeleteWordBackward)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteWordBackward));
            } else if (active_is_action(TuiAction::DeleteToLineStart)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineStart));
            } else if (active_is_action(TuiAction::DeleteToLineEnd)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineEnd));
            } else if (active_is_action(TuiAction::ClearInput)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::ClearInput));
            } else if (active_is_action(TuiAction::CursorLeft)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLeft));
            } else if (active_is_action(TuiAction::CursorRight)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorRight));
            } else if (active_is_action(TuiAction::CursorLineStart)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLineStart));
            } else if (active_is_action(TuiAction::CursorLineEnd)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorLineEnd));
            } else if (active_is_action(TuiAction::CursorWordLeft)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorWordLeft));
            } else if (active_is_action(TuiAction::CursorWordRight)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::CursorWordRight));
            } else if (active_is_action(TuiAction::Undo)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::Undo));
            } else if (active_is_action(TuiAction::Yank)) {
              static_cast<void>(apply_composer_draft_action(draft, TuiAction::Yank));
            }
            return render();
          }
          if (active_is_action(TuiAction::PageUp)) {
            auto const [_, height] = terminal_size();
            scroll_up(std::max<std::size_t>(1, height / 2));
            return render();
          }
          if (active_is_action(TuiAction::PageDown)) {
            auto const [_, height] = terminal_size();
            scroll_down(std::max<std::size_t>(1, height / 2));
            return render();
          }
          if (active_event.key == Key::MouseWheelUp) {
            scroll_up(3);
            return render();
          }
          if (active_event.key == Key::MouseWheelDown) {
            scroll_down(3);
            return render();
          }
          if (active_is_action(TuiAction::DetailsToggle)) {
            snapshot.tool_details_visible = !snapshot.tool_details_visible;
            snapshot.status = snapshot.tool_details_visible ? "tool details visible" : "tool details compact";
            return render();
          }
          if (active_is_action(TuiAction::VariantCycle)) {
            snapshot.status = "reasoning can be changed between turns";
            return render();
          }
          return true;
        };
        bool render_failed = false;
        while (submit_future.wait_for(kSpinnerFrameDelay) != std::future_status::ready) {
          if (terminal_signal_received()) {
            if (terminal_signal_number() == SIGINT && !draft.text.empty()) {
              clear_terminal_signal();
              static_cast<void>(clear_draft_for_interrupt());
              snapshot.selected_slash_command_index = selected_slash_command_index;
              if (!render()) {
                terminal_write_failed = true;
                render_failed = true;
                fail_pending_prompt_requests();
                break;
              }
              continue;
            }
            request_close_after_submit();
            break;
          }
          if (auto active_input = poll_curses_input()) {
            if (!handle_active_input(*active_input)) {
              terminal_write_failed = true;
              render_failed = true;
              fail_pending_prompt_requests();
              break;
            }
            if (close_after_submit) break;
          }
          static_cast<void>(service_pending_prompt_request(cancel_requested, request_stop));
          auto const drain_result = drain_runtime_events();
          if (drain_result == RuntimeEventDrainResult::RenderFailed) {
            terminal_write_failed = true;
            render_failed = true;
            fail_pending_prompt_requests();
            break;
          }
          if (drain_result == RuntimeEventDrainResult::Rendered) continue;
          if (!render_processing_frame()) {
            terminal_write_failed = true;
            render_failed = true;
            fail_pending_prompt_requests();
            break;
          }
        }
        result = submit_future.get();
        if (active_queues && active_queues->finish) {
          if (auto finished = active_queues->finish(run_cancel_requested.load()); !finished) {
            {
              std::lock_guard<std::recursive_mutex> lock(ui_mutex);
              snapshot.status = finished.error().format();
            }
            render_failed = true;
          }
        }
        if (drain_runtime_events() == RuntimeEventDrainResult::RenderFailed) {
          terminal_write_failed = true;
          render_failed = true;
        }
        {
          std::lock_guard<std::mutex> lock(prompt_audit_mutex);
          prompt_audit_sink = nullptr;
        }
        if (render_failed) return InputLoopAction::BreakLoop;
      }
      {
        std::lock_guard<std::mutex> lock(prompt_audit_mutex);
        prompt_audit_sink = nullptr;
      }
      if (terminal_signal_received()) return InputLoopAction::BreakLoop;
      auto const events_received = event_queue.received_any();
      if (events_received) {
        std::lock_guard<std::recursive_mutex> lock(ui_mutex);
        settle_turn_activity();
      }
      if (!events_received) {
        if (!is_slash_command) {
          push_transcript(snapshot, TranscriptItem{.label = "you", .text = submitted});
        }
        if (run_cancel_requested.load()) {
          push_transcript(
              snapshot,
              TranscriptItem{.label = "ava", .text = "stopped by user", .meta = assistant_meta_for_snapshot(snapshot)});
        } else {
          for (auto const& tool : result.tool_timeline) {
            push_transcript(snapshot, TranscriptItem{.tool = tool});
          }
          for (auto const& output : result.output) {
            push_transcript(
                snapshot,
                TranscriptItem{.label = "ava", .text = output, .meta = assistant_meta_for_snapshot(snapshot)});
          }
        }
      }
      transcript_scroll_offset = 0;
      snapshot.status = events_received ? (event_state.run_status == TuiEventRunStatus::Error      ? "error"
                                           : event_state.run_status == TuiEventRunStatus::Canceled ? "stopped"
                                                                                                   : "done")
                                        : (result.output.empty() ? "ok" : "done");
      snapshot.processing = false;
      refresh_token_status();
      if (!render()) {
        terminal_write_failed = true;
        return InputLoopAction::BreakLoop;
      }
      if (close_after_submit) return InputLoopAction::BreakLoop;
      if (result.quit) return InputLoopAction::BreakLoop;
      return InputLoopAction::ContinueLoop;
    }
    return InputLoopAction::None;
  };

  if (terminal_signal_received()) return 130;
  if (!render()) return 1;

  while (true) {
    auto const input = read_curses_input();
    if (terminal_signal_received()) {
      if (terminal_signal_number() == SIGINT && !draft.text.empty()) {
        clear_terminal_signal();
        static_cast<void>(clear_draft_for_interrupt());
        snapshot.selected_slash_command_index = selected_slash_command_index;
        if (!render()) {
          terminal_write_failed = true;
          break;
        }
        continue;
      }
      break;
    }
    if (input.resize) {
      if (!render()) {
        terminal_write_failed = true;
        break;
      }
      continue;
    }
    auto const event = input.event;
    auto is_action = [&](TuiAction action) { return key_matches_action(options.key_bindings, action, event.key); };
    auto select_slash_command = [&]() {
      selected_slash_command_index =
          clamp_slash_palette_selection(draft.text, snapshot.slash_commands, selected_slash_command_index);
      if (auto const disabled_reason = slash_command_selection_disabled_reason(draft.text, snapshot.slash_commands,
                                                                               selected_slash_command_index)) {
        snapshot.status = "command disabled: " + *disabled_reason;
        static_cast<void>(beep());
        return;
      }
      static_cast<void>(replace_composer_draft(
          draft, slash_command_selection_text(draft.text, snapshot.slash_commands, selected_slash_command_index)));
      selected_slash_command_index = 0;
      draft_scroll_offset = 0;
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
      draft_scroll_offset = 0;
      auto const text = input.text.empty() ? std::string(1, event.character) : input.text;
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
      draft_scroll_offset = 0;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteBackward));
    } else if (is_action(TuiAction::DeleteWordBackward)) {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteWordBackward));
    } else if (is_action(TuiAction::DeleteToLineStart)) {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineStart));
    } else if (is_action(TuiAction::DeleteToLineEnd)) {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
      static_cast<void>(apply_composer_draft_action(draft, TuiAction::DeleteToLineEnd));
    } else if (is_action(TuiAction::ClearInput)) {
      pending_escape_clear = false;
      history_index.reset();
      draft_input.clear();
      selected_slash_command_index = 0;
      slash_palette_suppressed = false;
      draft_scroll_offset = 0;
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
      if (!draft.text.empty()) {
        static_cast<void>(clear_draft_for_interrupt());
        snapshot.selected_slash_command_index = selected_slash_command_index;
        if (!render()) {
          terminal_write_failed = true;
          break;
        }
        continue;
      }
      break;
    } else if (is_action(TuiAction::Exit)) {
      break;
    } else if (is_action(TuiAction::VariantCycle)) {
      pending_escape_clear = false;
      cycle_reasoning();
    } else if (is_action(TuiAction::PageUp)) {
      auto const [_, height] = terminal_size();
      scroll_up(std::max<std::size_t>(1, height / 2));
    } else if (is_action(TuiAction::PageDown)) {
      auto const [_, height] = terminal_size();
      scroll_down(std::max<std::size_t>(1, height / 2));
    } else if (event.key == Key::MouseWheelUp) {
      scroll_up(3);
    } else if (event.key == Key::MouseWheelDown) {
      scroll_down(3);
    } else if (event.key == Key::MouseLeftClick) {
      pending_escape_clear = false;
      if (auto const clicked = slash_palette_selection_for_screen_row(snapshot, event.mouse_row)) {
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
      auto const matches = filter_slash_commands(draft.text, snapshot.slash_commands);
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
      history_index.reset();
      draft_input.clear();
      scroll_up(3);
    } else if (is_action(TuiAction::PaletteNext) && slash_palette_active()) {
      pending_escape_clear = false;
      auto const matches = filter_slash_commands(draft.text, snapshot.slash_commands);
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
      history_index.reset();
      draft_input.clear();
      scroll_down(3);
    } else if (is_action(TuiAction::Undo)) {
      pending_escape_clear = false;
      draft_scroll_offset = 0;
      snapshot.status = apply_composer_draft_action(draft, TuiAction::Undo) ? "undo" : "nothing to undo";
    } else if (is_action(TuiAction::Yank)) {
      pending_escape_clear = false;
      draft_scroll_offset = 0;
      snapshot.status = apply_composer_draft_action(draft, TuiAction::Yank) ? "yanked text" : "nothing to yank";
    } else if (is_action(TuiAction::DetailsToggle)) {
      pending_escape_clear = false;
      snapshot.tool_details_visible = !snapshot.tool_details_visible;
      snapshot.status = snapshot.tool_details_visible ? "tool details visible" : "tool details compact";
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
        snapshot.status.clear();
      } else if (!draft.text.empty()) {
        if (pending_escape_clear) {
          static_cast<void>(apply_composer_draft_action(draft, TuiAction::ClearInput));
          selected_slash_command_index = 0;
          history_index.reset();
          draft_input.clear();
          draft_scroll_offset = 0;
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
      auto const action = handle_submit();
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
