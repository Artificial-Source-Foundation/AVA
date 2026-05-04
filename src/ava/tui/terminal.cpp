#include "ava/tui/terminal.h"

#include <curses.h>
#include <unistd.h>

#if !defined(NCURSES_WIDECHAR) || NCURSES_WIDECHAR != 1
#error "AVA requires ncursesw with wide-character support."
#endif

#include <clocale>
#include <csignal>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "ava/core/error.h"

namespace ava::tui {
namespace {

volatile sig_atomic_t g_terminal_signal = 0;
struct sigaction g_curses_previous_sigint {};
struct sigaction g_curses_previous_sigterm {};

void mark_terminal_signal(int signal_number) { g_terminal_signal = signal_number; }

void install_curses_signal_flags() {
  struct sigaction action {};
  action.sa_handler = mark_terminal_signal;
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGINT);
  sigaddset(&action.sa_mask, SIGTERM);
  action.sa_flags = 0;
  static_cast<void>(sigaction(SIGINT, &action, &g_curses_previous_sigint));
  static_cast<void>(sigaction(SIGTERM, &action, &g_curses_previous_sigterm));
}

void uninstall_curses_signal_flags() {
  static_cast<void>(sigaction(SIGINT, &g_curses_previous_sigint, nullptr));
  static_cast<void>(sigaction(SIGTERM, &g_curses_previous_sigterm, nullptr));
}

void configure_curses_colors() {
  if (!has_colors()) return;
  static_cast<void>(start_color());
  static_cast<void>(use_default_colors());
}

void configure_curses_mouse() {
#ifdef NCURSES_MOUSE_VERSION
  if (!has_mouse()) return;
  mmask_t previous_mask = 0;
  mmask_t mask = BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED;
#ifdef BUTTON4_CLICKED
  mask |= BUTTON4_CLICKED;
#endif
#ifdef BUTTON4_RELEASED
  mask |= BUTTON4_RELEASED;
#endif
#ifdef BUTTON5_CLICKED
  mask |= BUTTON5_CLICKED;
#endif
#ifdef BUTTON5_RELEASED
  mask |= BUTTON5_RELEASED;
#endif
  static_cast<void>(mousemask(mask, &previous_mask));
#endif
}

void set_bracketed_paste(bool enabled) {
  static_cast<void>(std::fputs(enabled ? "\x1b[?2004h" : "\x1b[?2004l", stdout));
  static_cast<void>(std::fflush(stdout));
}

bool is_utf8_continuation(unsigned char byte) { return (byte & 0xC0U) == 0x80U; }

std::size_t utf8_sequence_length(unsigned char byte) {
  if ((byte & 0x80U) == 0) return 1;
  if (byte >= 0xC2U && byte <= 0xDFU) return 2;
  if ((byte & 0xF0U) == 0xE0U) return 3;
  if (byte >= 0xF0U && byte <= 0xF4U) return 4;
  return 0;
}

std::optional<int> parse_unsigned_int(std::string_view text, std::size_t& index) {
  if (index >= text.size() || text[index] < '0' || text[index] > '9') return std::nullopt;
  int value = 0;
  while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
    const auto digit = text[index] - '0';
    if (value > (std::numeric_limits<int>::max() - digit) / 10) return std::nullopt;
    value = (value * 10) + digit;
    ++index;
  }
  return value;
}

bool consume_char(std::string_view text, std::size_t& index, char expected) {
  if (index >= text.size() || text[index] != expected) return false;
  ++index;
  return true;
}

bool is_shift_enter_csi_u(std::string_view sequence) {
  if (!sequence.starts_with('[')) return false;
  auto index = std::size_t{1};
  const auto codepoint = parse_unsigned_int(sequence, index);
  if (!codepoint || *codepoint != 13) return false;
  if (!consume_char(sequence, index, ';')) return false;
  const auto modifiers = parse_unsigned_int(sequence, index);
  if (!modifiers || *modifiers != 2) return false;
  return index + 1 == sequence.size() && (sequence[index] == 'u' || sequence[index] == '~');
}

bool is_legacy_shift_enter_sequence(std::string_view sequence) {
  if (!sequence.starts_with('[')) return false;
  auto index = std::size_t{1};
  const auto escape_code = parse_unsigned_int(sequence, index);
  if (!escape_code || *escape_code != 27) return false;
  if (!consume_char(sequence, index, ';')) return false;
  const auto modifiers = parse_unsigned_int(sequence, index);
  if (!modifiers || *modifiers != 2) return false;
  if (!consume_char(sequence, index, ';')) return false;
  const auto key_code = parse_unsigned_int(sequence, index);
  return key_code && *key_code == 13 && index + 1 == sequence.size() && sequence[index] == '~';
}

}  // namespace

CursesSession::CursesSession(void* screen) : screen_(screen), active_(screen != nullptr) {}

CursesSession::CursesSession(CursesSession&& other) noexcept
    : screen_(std::move(other.screen_)),
      previous_locale_(std::move(other.previous_locale_)),
      active_(std::exchange(other.active_, false)) {}

CursesSession& CursesSession::operator=(CursesSession&& other) noexcept {
  if (this == &other) return *this;
  restore();
  screen_ = std::move(other.screen_);
  previous_locale_ = std::move(other.previous_locale_);
  active_ = std::exchange(other.active_, false);
  return *this;
}

CursesSession::~CursesSession() { restore(); }

ava::core::Result<CursesSession> CursesSession::enter() {
  const char* current_locale = std::setlocale(LC_ALL, nullptr);
  const std::string previous_locale = current_locale == nullptr ? "C" : current_locale;
  if (std::setlocale(LC_ALL, "") == nullptr) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to configure terminal locale"));
  }

  sigset_t blocked_signals{};
  sigset_t previous_mask{};
  sigemptyset(&blocked_signals);
  sigaddset(&blocked_signals, SIGINT);
  sigaddset(&blocked_signals, SIGTERM);
  const bool blocked = sigprocmask(SIG_BLOCK, &blocked_signals, &previous_mask) == 0;
  auto restore_signal_mask = [&]() {
    if (blocked) static_cast<void>(sigprocmask(SIG_SETMASK, &previous_mask, nullptr));
  };

  SCREEN* screen = newterm(nullptr, stdout, stdin);
  if (screen == nullptr) {
    restore_signal_mask();
    static_cast<void>(std::setlocale(LC_ALL, previous_locale.c_str()));
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to initialize ncurses screen"));
  }

  CursesSession session(screen);
  session.previous_locale_ = previous_locale;
  static_cast<void>(set_term(screen));
  install_curses_signal_flags();

  if (cbreak() == ERR || noecho() == ERR || keypad(stdscr, TRUE) == ERR) {
    session.restore();
    restore_signal_mask();
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to configure ncurses terminal mode"));
  }

  noqiflush();
  static_cast<void>(nonl());
#ifdef NCURSES_VERSION
  static_cast<void>(set_escdelay(100));
#endif
  configure_curses_colors();
  configure_curses_mouse();
  set_bracketed_paste(true);
  restore_signal_mask();
  return session;
}

void CursesSession::restore() noexcept {
  if (!active_) return;
  static_cast<void>(set_term(static_cast<SCREEN*>(screen_.get())));
  set_bracketed_paste(false);
  static_cast<void>(curs_set(1));
  static_cast<void>(endwin());
  uninstall_curses_signal_flags();
  active_ = false;
  screen_.reset();
  if (!previous_locale_.empty()) static_cast<void>(std::setlocale(LC_ALL, previous_locale_.c_str()));
}

void CursesSession::ScreenDeleter::operator()(void* screen) const noexcept {
  if (screen == nullptr) return;
  delscreen(static_cast<SCREEN*>(screen));
}

void erase_last_utf8_codepoint(std::string& text) {
  if (text.empty()) return;
  if (!is_utf8_continuation(static_cast<unsigned char>(text.back()))) {
    text.pop_back();
    return;
  }

  auto start = text.size();
  while (start > 0 && is_utf8_continuation(static_cast<unsigned char>(text[start - 1]))) {
    --start;
  }
  if (start == 0) {
    text.pop_back();
    return;
  }

  const auto expected_length = utf8_sequence_length(static_cast<unsigned char>(text[start - 1]));
  const auto actual_length = text.size() - (start - 1);
  if (expected_length > 1 && expected_length == actual_length) {
    text.erase(start - 1);
  } else {
    text.pop_back();
  }
}

Key terminal_escape_sequence_key(std::string_view sequence) {
  if (is_legacy_shift_enter_sequence(sequence) || is_shift_enter_csi_u(sequence)) return Key::ShiftEnter;
  return Key::Unknown;
}

bool terminal_is_tty() { return isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0; }

bool terminal_signal_received() { return g_terminal_signal != 0; }

int terminal_signal_number() { return g_terminal_signal; }

void clear_terminal_signal() { g_terminal_signal = 0; }

}  // namespace ava::tui
