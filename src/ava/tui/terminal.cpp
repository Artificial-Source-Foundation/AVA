#include "ava/tui/terminal.h"

#include <curses.h>
#include <unistd.h>

#if !defined(NCURSES_WIDECHAR) || NCURSES_WIDECHAR != 1
#error "AVA requires ncursesw with wide-character support."
#endif

#include "ava/core/error.h"

#include <clocale>
#include <csignal>
#include <cstdio>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace ava::tui {
namespace {

constexpr int kTerminalEscapeDelayMs = 100;

sig_atomic_t volatile g_terminal_signal = 0;
struct sigaction g_curses_previous_sigint{};
struct sigaction g_curses_previous_sigterm{};

void mark_terminal_signal(int signal_number)
{
  g_terminal_signal = signal_number;
}

void install_curses_signal_flags()
{
  struct sigaction action{};
  action.sa_handler = mark_terminal_signal;
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGINT);
  sigaddset(&action.sa_mask, SIGTERM);
  action.sa_flags = 0;
  static_cast<void>(sigaction(SIGINT, &action, &g_curses_previous_sigint));
  static_cast<void>(sigaction(SIGTERM, &action, &g_curses_previous_sigterm));
}

void uninstall_curses_signal_flags()
{
  static_cast<void>(sigaction(SIGINT, &g_curses_previous_sigint, nullptr));
  static_cast<void>(sigaction(SIGTERM, &g_curses_previous_sigterm, nullptr));
}

void configure_curses_colors()
{
  if (!has_colors())
    return;
  static_cast<void>(start_color());
  static_cast<void>(use_default_colors());
}

void configure_curses_mouse()
{
#ifdef NCURSES_MOUSE_VERSION
  if (!has_mouse())
    return;
  mmask_t previous_mask = 0;
  mmask_t mask = BUTTON1_CLICKED | BUTTON4_PRESSED | BUTTON5_PRESSED;
  static_cast<void>(mousemask(mask, &previous_mask));
#endif
}

void set_bracketed_paste(bool enabled)
{
  static_cast<void>(std::fputs(enabled ? "\x1b[?2004h" : "\x1b[?2004l", stdout));
  static_cast<void>(std::fflush(stdout));
}

bool is_utf8_continuation(unsigned char byte)
{
  return (byte & 0xC0U) == 0x80U;
}

std::size_t utf8_sequence_length(unsigned char byte)
{
  if ((byte & 0x80U) == 0)
    return 1;
  if (byte >= 0xC2U && byte <= 0xDFU)
    return 2;
  if ((byte & 0xF0U) == 0xE0U)
    return 3;
  if (byte >= 0xF0U && byte <= 0xF4U)
    return 4;
  return 0;
}

std::optional<int> parse_unsigned_int(std::string_view text, std::size_t& index)
{
  if (index >= text.size() || text[index] < '0' || text[index] > '9')
    return std::nullopt;
  int value = 0;
  while (index < text.size() && text[index] >= '0' && text[index] <= '9')
  {
    auto const digit = text[index] - '0';
    if (value > (std::numeric_limits<int>::max() - digit) / 10)
      return std::nullopt;
    value = (value * 10) + digit;
    ++index;
  }
  return value;
}

bool consume_char(std::string_view text, std::size_t& index, char expected)
{
  if (index >= text.size() || text[index] != expected)
    return false;
  ++index;
  return true;
}

bool is_modified_enter_csi_u(std::string_view sequence, int expected_modifiers)
{
  if (!sequence.starts_with('['))
    return false;
  auto index = std::size_t{1};
  auto const codepoint = parse_unsigned_int(sequence, index);
  if (!codepoint || *codepoint != 13)
    return false;
  if (!consume_char(sequence, index, ';'))
    return false;
  auto const modifiers = parse_unsigned_int(sequence, index);
  if (!modifiers || *modifiers != expected_modifiers)
    return false;
  return index + 1 == sequence.size() && (sequence[index] == 'u' || sequence[index] == '~');
}

bool is_shift_enter_csi_u(std::string_view sequence)
{
  return is_modified_enter_csi_u(sequence, 2);
}

bool is_ctrl_enter_csi_u(std::string_view sequence)
{
  return is_modified_enter_csi_u(sequence, 5);
}

bool is_alt_enter_csi_u(std::string_view sequence)
{
  return is_modified_enter_csi_u(sequence, 3);
}

bool is_ctrl_minus_csi_u(std::string_view sequence)
{
  if (!sequence.starts_with('['))
    return false;
  auto index = std::size_t{1};
  auto const codepoint = parse_unsigned_int(sequence, index);
  if (!codepoint || *codepoint != 45)
    return false;
  if (!consume_char(sequence, index, ';'))
    return false;
  auto const modifiers = parse_unsigned_int(sequence, index);
  if (!modifiers || *modifiers != 5)
    return false;
  return index + 1 == sequence.size() && (sequence[index] == 'u' || sequence[index] == '~');
}

bool is_ctrl_shift_p_csi_u(std::string_view sequence)
{
  if (!sequence.starts_with('['))
    return false;
  auto index = std::size_t{1};
  auto const codepoint = parse_unsigned_int(sequence, index);
  if (!codepoint || (*codepoint != 'P' && *codepoint != 'p'))
    return false;
  if (!consume_char(sequence, index, ';'))
    return false;
  auto const modifiers = parse_unsigned_int(sequence, index);
  if (!modifiers || *modifiers != 6)
    return false;
  return index + 1 == sequence.size() && (sequence[index] == 'u' || sequence[index] == '~');
}

bool is_alt_delete_sequence(std::string_view sequence)
{
  if (!sequence.starts_with('['))
    return false;
  auto index = std::size_t{1};
  auto const codepoint = parse_unsigned_int(sequence, index);
  if (!codepoint || *codepoint != 3)
    return false;
  if (!consume_char(sequence, index, ';'))
    return false;
  auto const modifiers = parse_unsigned_int(sequence, index);
  if (!modifiers || *modifiers != 3)
    return false;
  return index + 1 == sequence.size() && sequence[index] == '~';
}

bool is_legacy_modified_enter_sequence(std::string_view sequence, int expected_modifiers)
{
  if (!sequence.starts_with('['))
    return false;
  auto index = std::size_t{1};
  auto const escape_code = parse_unsigned_int(sequence, index);
  if (!escape_code || *escape_code != 27)
    return false;
  if (!consume_char(sequence, index, ';'))
    return false;
  auto const modifiers = parse_unsigned_int(sequence, index);
  if (!modifiers || *modifiers != expected_modifiers)
    return false;
  if (!consume_char(sequence, index, ';'))
    return false;
  auto const key_code = parse_unsigned_int(sequence, index);
  return key_code && *key_code == 13 && index + 1 == sequence.size() && sequence[index] == '~';
}

bool is_legacy_shift_enter_sequence(std::string_view sequence)
{
  return is_legacy_modified_enter_sequence(sequence, 2);
}

bool is_legacy_ctrl_enter_sequence(std::string_view sequence)
{
  return is_legacy_modified_enter_sequence(sequence, 5);
}

bool is_legacy_alt_enter_sequence(std::string_view sequence)
{
  return is_legacy_modified_enter_sequence(sequence, 3);
}

Key page_key_from_csi_tilde(std::string_view sequence)
{
  if (!sequence.starts_with('[') || sequence.empty() || sequence.back() != '~')
    return Key::Unknown;

  auto index = std::size_t{1};
  auto const code = parse_unsigned_int(sequence, index);
  if (!code)
    return Key::Unknown;

  while (index + 1 < sequence.size())
  {
    if (!consume_char(sequence, index, ';') && !consume_char(sequence, index, ':'))
      return Key::Unknown;
    if (!parse_unsigned_int(sequence, index))
      return Key::Unknown;
  }
  if (index + 1 != sequence.size())
    return Key::Unknown;

  if (*code == 3)
    return Key::Delete;
  if (*code == 1 || *code == 7)
    return Key::Home;
  if (*code == 4 || *code == 8)
    return Key::End;
  if (*code == 5)
    return Key::PageUp;
  if (*code == 6)
    return Key::PageDown;
  return Key::Unknown;
}

Key sgr_mouse_key(std::string_view sequence)
{
  if (!sequence.starts_with("[<") || sequence.size() < 7)
    return Key::Unknown;
  auto index = std::size_t{2};
  auto const button = parse_unsigned_int(sequence, index);
  if (!button || !consume_char(sequence, index, ';'))
    return Key::Unknown;
  if (!parse_unsigned_int(sequence, index) || !consume_char(sequence, index, ';'))
    return Key::Unknown;
  if (!parse_unsigned_int(sequence, index))
    return Key::Unknown;
  if (index + 1 != sequence.size() || sequence[index] != 'M')
    return Key::Unknown;

  if ((*button & 64) == 0)
    return Key::Unknown;
  return (*button & 1) == 0 ? Key::MouseWheelUp : Key::MouseWheelDown;
}

bool is_legacy_mouse_sequence(std::string_view sequence)
{
  return sequence.starts_with("[M") && sequence.size() >= 5;
}

Key legacy_mouse_key(std::string_view sequence)
{
  if (!is_legacy_mouse_sequence(sequence))
    return Key::Unknown;
  auto const button_byte = static_cast<unsigned char>(sequence[2]);
  if (button_byte < 32)
    return Key::Unknown;
  auto const button = static_cast<unsigned int>(button_byte - 32U);
  if ((button & 64U) == 0)
    return Key::Unknown;
  return (button & 1U) == 0 ? Key::MouseWheelUp : Key::MouseWheelDown;
}

bool is_csi_final_byte(unsigned char byte)
{
  return byte >= 0x40U && byte <= 0x7EU;
}

bool is_control_string_intro(char ch)
{
  return ch == ']' || ch == 'P' || ch == '^' || ch == '_' || ch == 'X';
}

bool ends_with_string_terminator(std::string_view sequence)
{
  return sequence.size() >= 2 && sequence[sequence.size() - 2] == '\x1b' && sequence.back() == '\\';
}

bool is_control_string_complete(std::string_view sequence)
{
  if (sequence.empty() || !is_control_string_intro(sequence.front()))
    return false;
  if (ends_with_string_terminator(sequence))
    return true;
  return sequence.front() == ']' && sequence.find('\a') != std::string_view::npos;
}

}  // namespace

CursesSession::CursesSession(void* screen) : screen_(screen), active_(screen != nullptr)
{
}

CursesSession::CursesSession(CursesSession&& other) noexcept
    : screen_(std::move(other.screen_)),
      previous_locale_(std::move(other.previous_locale_)),
      previous_terminal_attrs_(other.previous_terminal_attrs_),
      restore_terminal_attrs_(std::exchange(other.restore_terminal_attrs_, false)),
      active_(std::exchange(other.active_, false))
{
}

CursesSession& CursesSession::operator=(CursesSession&& other) noexcept
{
  if (this == &other)
    return *this;
  restore();
  screen_ = std::move(other.screen_);
  previous_locale_ = std::move(other.previous_locale_);
  previous_terminal_attrs_ = other.previous_terminal_attrs_;
  restore_terminal_attrs_ = std::exchange(other.restore_terminal_attrs_, false);
  active_ = std::exchange(other.active_, false);
  return *this;
}

CursesSession::~CursesSession()
{
  restore();
}

ava::core::Result<CursesSession> CursesSession::enter()
{
  char const* current_locale = std::setlocale(LC_ALL, nullptr);
  std::string const previous_locale = current_locale == nullptr ? "C" : current_locale;
  if (std::setlocale(LC_ALL, "") == nullptr)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to configure terminal locale"));
  }

  sigset_t blocked_signals{};
  sigset_t previous_mask{};
  sigemptyset(&blocked_signals);
  sigaddset(&blocked_signals, SIGINT);
  sigaddset(&blocked_signals, SIGTERM);
  bool const blocked = sigprocmask(SIG_BLOCK, &blocked_signals, &previous_mask) == 0;
  auto restore_signal_mask = [&]() {
    if (blocked)
      static_cast<void>(sigprocmask(SIG_SETMASK, &previous_mask, nullptr));
  };

  SCREEN* screen = newterm(nullptr, stdout, stdin);
  if (screen == nullptr)
  {
    restore_signal_mask();
    static_cast<void>(std::setlocale(LC_ALL, previous_locale.c_str()));
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to initialize ncurses screen"));
  }

  CursesSession session(screen);
  session.previous_locale_ = previous_locale;
  static_cast<void>(set_term(screen));
  install_curses_signal_flags();

  if (raw() == ERR || noecho() == ERR || keypad(stdscr, TRUE) == ERR)
  {
    session.restore();
    restore_signal_mask();
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to configure ncurses raw terminal mode"));
  }

  termios terminal_attrs{};
  if (tcgetattr(STDIN_FILENO, &terminal_attrs) == 0)
  {
    session.previous_terminal_attrs_ = terminal_attrs;
    terminal_attrs.c_iflag &= static_cast<tcflag_t>(~(IXON | IXOFF | IXANY));
    if (tcsetattr(STDIN_FILENO, TCSANOW, &terminal_attrs) == 0)
      session.restore_terminal_attrs_ = true;
  }

  noqiflush();
  static_cast<void>(nonl());
  static_cast<void>(scrollok(stdscr, FALSE));
  static_cast<void>(idlok(stdscr, FALSE));
#ifdef NCURSES_VERSION
  static_cast<void>(set_escdelay(terminal_escape_delay_ms()));
#endif
  configure_curses_colors();
  configure_curses_mouse();
  set_bracketed_paste(true);
  restore_signal_mask();
  return session;
}

void CursesSession::restore() noexcept
{
  if (!active_)
    return;
  static_cast<void>(set_term(static_cast<SCREEN*>(screen_.get())));
  set_bracketed_paste(false);
  if (restore_terminal_attrs_)
  {
    static_cast<void>(tcsetattr(STDIN_FILENO, TCSANOW, &previous_terminal_attrs_));
    restore_terminal_attrs_ = false;
  }
  static_cast<void>(curs_set(1));
  static_cast<void>(endwin());
  uninstall_curses_signal_flags();
  active_ = false;
  screen_.reset();
  if (!previous_locale_.empty())
    static_cast<void>(std::setlocale(LC_ALL, previous_locale_.c_str()));
}

void CursesSession::ScreenDeleter::operator()(void* screen) const noexcept
{
  if (screen == nullptr)
    return;
  delscreen(static_cast<SCREEN*>(screen));
}

void erase_last_utf8_codepoint(std::string& text)
{
  if (text.empty())
    return;
  if (!is_utf8_continuation(static_cast<unsigned char>(text.back())))
  {
    text.pop_back();
    return;
  }

  auto start = text.size();
  while (start > 0 && is_utf8_continuation(static_cast<unsigned char>(text[start - 1])))
  {
    --start;
  }
  if (start == 0)
  {
    text.pop_back();
    return;
  }

  auto const expected_length = utf8_sequence_length(static_cast<unsigned char>(text[start - 1]));
  auto const actual_length = text.size() - (start - 1);
  if (expected_length > 1 && expected_length == actual_length)
  {
    text.erase(start - 1);
  }
  else
  {
    text.pop_back();
  }
}

int terminal_escape_delay_ms()
{
  return kTerminalEscapeDelayMs;
}

Key terminal_escape_sequence_key(std::string_view sequence)
{
  if (sequence == "\x7f" || sequence == "\b")
    return Key::AltBackspace;
  if (sequence == "\x1d")
    return Key::CtrlAltRightBracket;
  if (sequence == "\x1f" || is_ctrl_minus_csi_u(sequence))
    return Key::CtrlMinus;
  if (sequence == "\r" || sequence == "\n")
    return Key::AltEnter;
  if (sequence == "[Z")
    return Key::ShiftTab;
  if (sequence == "[1;6P" || is_ctrl_shift_p_csi_u(sequence))
    return Key::CtrlShiftP;
  if (is_alt_delete_sequence(sequence))
    return Key::AltDelete;
  if (sequence == "b" || sequence == "B")
    return Key::AltB;
  if (sequence == "d" || sequence == "D")
    return Key::AltD;
  if (sequence == "f" || sequence == "F")
    return Key::AltF;
  if (sequence == "h" || sequence == "H")
    return Key::AltH;
  if (sequence == "j" || sequence == "J")
    return Key::AltJ;
  if (sequence == "k" || sequence == "K")
    return Key::AltK;
  if (sequence == "l" || sequence == "L")
    return Key::AltL;
  if (sequence == "w" || sequence == "W")
    return Key::AltW;
  if (sequence == "y" || sequence == "Y")
    return Key::AltY;
  if (sequence == "[1;5D" || sequence == "[5D")
    return Key::CtrlArrowLeft;
  if (sequence == "[1;5C" || sequence == "[5C")
    return Key::CtrlArrowRight;
  if (sequence == "[1;3D" || sequence == "[3D")
    return Key::AltArrowLeft;
  if (sequence == "[1;3C" || sequence == "[3C")
    return Key::AltArrowRight;
  if (sequence == "[1;3A" || sequence == "[3A")
    return Key::AltArrowUp;
  if (sequence == "[H" || sequence == "OH")
    return Key::Home;
  if (sequence == "[F" || sequence == "OF")
    return Key::End;
  if (is_legacy_shift_enter_sequence(sequence) || is_shift_enter_csi_u(sequence))
    return Key::ShiftEnter;
  if (is_legacy_ctrl_enter_sequence(sequence) || is_ctrl_enter_csi_u(sequence))
    return Key::CtrlEnter;
  if (is_legacy_alt_enter_sequence(sequence) || is_alt_enter_csi_u(sequence))
    return Key::AltEnter;
  if (auto const key = page_key_from_csi_tilde(sequence); key != Key::Unknown)
    return key;
  if (auto const key = sgr_mouse_key(sequence); key != Key::Unknown)
    return key;
  if (auto const key = legacy_mouse_key(sequence); key != Key::Unknown)
    return key;
  return Key::Unknown;
}

bool terminal_escape_sequence_complete(std::string_view sequence)
{
  if (sequence.empty())
    return false;
  if (is_control_string_complete(sequence))
    return true;
  if (is_control_string_intro(sequence.front()))
    return false;

  if (sequence.starts_with('['))
  {
    if (sequence.size() <= 1)
      return false;
    if (sequence.starts_with("[M"))
      return is_legacy_mouse_sequence(sequence);
    return is_csi_final_byte(static_cast<unsigned char>(sequence.back()));
  }

  if (sequence.starts_with('O'))
  {
    if (sequence.size() <= 1)
      return false;
    return is_csi_final_byte(static_cast<unsigned char>(sequence.back()));
  }

  if (sequence.starts_with('#'))
  {
    if (sequence.size() <= 1)
      return false;
    return is_csi_final_byte(static_cast<unsigned char>(sequence.back()));
  }

  if (sequence.size() == 1)
  {
    auto const byte = static_cast<unsigned char>(sequence.front());
    if (byte == 0x08U || byte == 0x0AU || byte == 0x0DU || byte == 0x1DU || byte == 0x1FU || byte == 0x7FU)
      return true;
    return byte >= 0x30U && byte <= 0x7EU;
  }

  return is_csi_final_byte(static_cast<unsigned char>(sequence.back()));
}

bool terminal_escape_sequence_should_discard(std::string_view sequence)
{
  if (!terminal_escape_sequence_complete(sequence))
    return false;
  if (sequence == "[200~")
    return false;
  return terminal_escape_sequence_key(sequence) == Key::Unknown;
}

bool terminal_is_tty()
{
  return isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0;
}

bool terminal_signal_received()
{
  return g_terminal_signal != 0;
}

int terminal_signal_number()
{
  return g_terminal_signal;
}

void clear_terminal_signal()
{
  g_terminal_signal = 0;
}

}  // namespace ava::tui
