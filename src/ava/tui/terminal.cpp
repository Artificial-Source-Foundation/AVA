#include "sys.h"
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
constexpr std::string_view kKittyKeyboardPushSequence = "\x1b[>5u";
constexpr std::string_view kKittyKeyboardQuerySequence = "\x1b[>5u\x1b[?u\x1b[c";
constexpr std::string_view kKittyKeyboardPopSequence = "\x1b[<u";
constexpr std::string_view kModifyOtherKeysEnableSequence = "\x1b[>4;2m";
constexpr std::string_view kModifyOtherKeysDisableSequence = "\x1b[>4;0m";

sig_atomic_t volatile g_terminal_signal = 0;
bool g_keyboard_protocol_kitty_response_seen = false;
bool g_modify_other_keys_enabled = false;
struct sigaction g_curses_previous_sigint{};
struct sigaction g_curses_previous_sigterm{};

InputEvent key_event(Key key)
{
  return InputEvent{.key = key, .character = '\0', .text = {}, .mouse_column = 0, .mouse_row = 0};
}

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

void write_terminal_sequence(std::string_view sequence)
{
  static_cast<void>(std::fwrite(sequence.data(), 1, sequence.size(), stdout));
  static_cast<void>(std::fflush(stdout));
}

void push_kitty_keyboard_protocol()
{
  write_terminal_sequence(kKittyKeyboardQuerySequence);
}

void pop_kitty_keyboard_protocol()
{
  write_terminal_sequence(kKittyKeyboardPopSequence);
}

void reset_keyboard_protocol_negotiation()
{
  g_keyboard_protocol_kitty_response_seen = false;
  g_modify_other_keys_enabled = false;
}

void enable_modify_other_keys_fallback()
{
  if (g_modify_other_keys_enabled)
    return;
  write_terminal_sequence(kModifyOtherKeysEnableSequence);
  g_modify_other_keys_enabled = true;
}

void disable_modify_other_keys_fallback()
{
  if (!g_modify_other_keys_enabled)
    return;
  write_terminal_sequence(kModifyOtherKeysDisableSequence);
  g_modify_other_keys_enabled = false;
}

void apply_keyboard_protocol_response_action(KeyboardProtocolResponseAction action)
{
  switch (action)
  {
    case KeyboardProtocolResponseAction::EnableModifyOtherKeys:
      enable_modify_other_keys_fallback();
      break;
    case KeyboardProtocolResponseAction::DisableModifyOtherKeys:
      disable_modify_other_keys_fallback();
      break;
    case KeyboardProtocolResponseAction::None:
      break;
  }
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

struct KittyCsiUSequence
{
  int codepoint = 0;
  std::optional<int> shifted_key = std::nullopt;
  std::optional<int> base_layout_key = std::nullopt;
  int modifiers = 0;
  int event_type = 1;
};

struct ModifyOtherKeysSequence
{
  int codepoint = 0;
  int modifiers = 0;
};

std::optional<int> parse_optional_unsigned_int(std::string_view text, std::size_t& index)
{
  if (index >= text.size() || text[index] < '0' || text[index] > '9')
    return std::nullopt;
  return parse_unsigned_int(text, index);
}

std::optional<KittyCsiUSequence> parse_kitty_csi_u_sequence(std::string_view sequence)
{
  if (!sequence.starts_with('[') || sequence.size() < 3 || sequence.back() != 'u')
    return std::nullopt;

  auto index = std::size_t{1};
  auto const codepoint = parse_unsigned_int(sequence, index);
  if (!codepoint)
    return std::nullopt;

  auto shifted_key = std::optional<int>{};
  auto base_layout_key = std::optional<int>{};
  if (index < sequence.size() && sequence[index] == ':')
  {
    ++index;
    shifted_key = parse_optional_unsigned_int(sequence, index);
    if (index < sequence.size() && sequence[index] == ':')
    {
      ++index;
      base_layout_key = parse_optional_unsigned_int(sequence, index);
      if (!base_layout_key)
        return std::nullopt;
    }
  }

  auto modifier_value = 1;
  auto event_type = 1;
  if (index < sequence.size() && sequence[index] == ';')
  {
    ++index;
    auto const parsed_modifier = parse_unsigned_int(sequence, index);
    if (!parsed_modifier || *parsed_modifier <= 0)
      return std::nullopt;
    modifier_value = *parsed_modifier;

    if (index < sequence.size() && sequence[index] == ':')
    {
      ++index;
      auto const parsed_event_type = parse_unsigned_int(sequence, index);
      if (!parsed_event_type || *parsed_event_type <= 0)
        return std::nullopt;
      event_type = *parsed_event_type;
    }
  }

  if (index + 1 != sequence.size() || sequence[index] != 'u')
    return std::nullopt;

  return KittyCsiUSequence{
      .codepoint = *codepoint, .shifted_key = shifted_key, .base_layout_key = base_layout_key, .modifiers = modifier_value - 1, .event_type = event_type};
}

constexpr int kKittyModifierShift = 1;
constexpr int kKittyModifierAlt = 2;
constexpr int kKittyModifierCtrl = 4;
constexpr int kKittyModifierSuper = 8;
constexpr int kKittyLockModifiers = 64 + 128;

int effective_kitty_modifiers(int modifiers)
{
  return modifiers & ~kKittyLockModifiers;
}

Key cursor_key_from_direction_and_modifiers(int direction, int modifiers)
{
  auto const effective_modifiers = effective_kitty_modifiers(modifiers);
  if ((effective_modifiers & ~(kKittyModifierShift | kKittyModifierAlt | kKittyModifierCtrl)) != 0)
    return Key::Unknown;

  auto const has_shift = (effective_modifiers & kKittyModifierShift) != 0;
  auto const has_alt = (effective_modifiers & kKittyModifierAlt) != 0;
  auto const has_ctrl = (effective_modifiers & kKittyModifierCtrl) != 0;
  switch (direction)
  {
    case -4:
      if (has_ctrl && has_shift && !has_alt)
        return Key::ShiftCtrlArrowLeft;
      if (has_alt && has_shift && !has_ctrl)
        return Key::ShiftAltArrowLeft;
      if (has_ctrl && !has_alt && !has_shift)
        return Key::CtrlArrowLeft;
      if (has_alt && !has_ctrl && !has_shift)
        return Key::AltArrowLeft;
      if (has_shift && !has_ctrl && !has_alt)
        return Key::ShiftArrowLeft;
      return effective_modifiers == 0 ? Key::ArrowLeft : Key::Unknown;
    case -3:
      if (has_ctrl && has_shift && !has_alt)
        return Key::ShiftCtrlArrowRight;
      if (has_alt && has_shift && !has_ctrl)
        return Key::ShiftAltArrowRight;
      if (has_ctrl && !has_alt && !has_shift)
        return Key::CtrlArrowRight;
      if (has_alt && !has_ctrl && !has_shift)
        return Key::AltArrowRight;
      if (has_shift && !has_ctrl && !has_alt)
        return Key::ShiftArrowRight;
      return effective_modifiers == 0 ? Key::ArrowRight : Key::Unknown;
    case -1:
      if (has_shift && !has_ctrl && !has_alt)
        return Key::ShiftArrowUp;
      if (has_alt && !has_ctrl && !has_shift)
        return Key::AltArrowUp;
      return effective_modifiers == 0 ? Key::ArrowUp : Key::Unknown;
    case -2:
      if (has_shift && !has_ctrl && !has_alt)
        return Key::ShiftArrowDown;
      if (has_alt && !has_ctrl && !has_shift)
        return Key::AltArrowDown;
      return effective_modifiers == 0 ? Key::ArrowDown : Key::Unknown;
    default:
      return Key::Unknown;
  }
}

Key home_end_key_from_modifiers(bool home, int modifiers)
{
  auto const effective_modifiers = effective_kitty_modifiers(modifiers);
  if ((effective_modifiers & ~(kKittyModifierShift | kKittyModifierCtrl)) != 0)
    return Key::Unknown;
  auto const has_shift = (effective_modifiers & kKittyModifierShift) != 0;
  auto const has_ctrl = (effective_modifiers & kKittyModifierCtrl) != 0;
  if (has_shift && has_ctrl)
    return home ? Key::ShiftCtrlHome : Key::ShiftCtrlEnd;
  if (has_ctrl)
    return home ? Key::CtrlHome : Key::CtrlEnd;
  if (has_shift)
    return home ? Key::ShiftHome : Key::ShiftEnd;
  return home ? Key::Home : Key::End;
}

Key csi_home_end_key(std::string_view sequence)
{
  if (sequence == "OH")
    return Key::Home;
  if (sequence == "OF")
    return Key::End;
  if (!sequence.starts_with('[') || sequence.size() < 2 || (sequence.back() != 'H' && sequence.back() != 'F'))
    return Key::Unknown;
  auto const home = sequence.back() == 'H';
  if (sequence.size() == 2)
    return home_end_key_from_modifiers(home, 0);

  auto index = std::size_t{1};
  auto const first = parse_unsigned_int(sequence, index);
  if (!first)
    return Key::Unknown;
  auto modifier_value = *first;
  if (index < sequence.size() && sequence[index] == ';')
  {
    if (*first != 1)
      return Key::Unknown;
    ++index;
    auto const parsed_modifier = parse_unsigned_int(sequence, index);
    if (!parsed_modifier)
      return Key::Unknown;
    modifier_value = *parsed_modifier;
  }
  if (modifier_value <= 0 || index + 1 != sequence.size())
    return Key::Unknown;
  return home_end_key_from_modifiers(home, modifier_value - 1);
}

Key csi_cursor_key(std::string_view sequence)
{
  if (sequence.size() == 2 && sequence[0] == 'O')
  {
    switch (sequence[1])
    {
      case 'A':
        return Key::ArrowUp;
      case 'B':
        return Key::ArrowDown;
      case 'C':
        return Key::ArrowRight;
      case 'D':
        return Key::ArrowLeft;
      default:
        return Key::Unknown;
    }
  }
  if (!sequence.starts_with('[') || sequence.size() < 2)
    return Key::Unknown;

  auto const final = sequence.back();
  auto direction = 0;
  switch (final)
  {
    case 'A':
      direction = -1;
      break;
    case 'B':
      direction = -2;
      break;
    case 'C':
      direction = -3;
      break;
    case 'D':
      direction = -4;
      break;
    default:
      return Key::Unknown;
  }
  if (sequence.size() == 2)
    return cursor_key_from_direction_and_modifiers(direction, 0);

  auto index = std::size_t{1};
  auto first = parse_unsigned_int(sequence, index);
  if (!first)
    return Key::Unknown;
  auto modifier_value = *first;
  if (index < sequence.size() && sequence[index] == ';')
  {
    if (*first != 1)
      return Key::Unknown;
    ++index;
    auto const parsed_modifier = parse_unsigned_int(sequence, index);
    if (!parsed_modifier)
      return Key::Unknown;
    modifier_value = *parsed_modifier;
  }
  if (modifier_value <= 0 || index + 1 != sequence.size())
    return Key::Unknown;
  return cursor_key_from_direction_and_modifiers(direction, modifier_value - 1);
}

int normalize_kitty_functional_codepoint(int codepoint)
{
  switch (codepoint)
  {
    case 57399:
      return '0';
    case 57400:
      return '1';
    case 57401:
      return '2';
    case 57402:
      return '3';
    case 57403:
      return '4';
    case 57404:
      return '5';
    case 57405:
      return '6';
    case 57406:
      return '7';
    case 57407:
      return '8';
    case 57408:
      return '9';
    case 57409:
      return '.';
    case 57410:
      return '/';
    case 57411:
      return '*';
    case 57412:
      return '-';
    case 57413:
      return '+';
    case 57415:
      return '=';
    case 57416:
      return ',';
    case 57417:
      return -4;
    case 57418:
      return -3;
    case 57419:
      return -1;
    case 57420:
      return -2;
    case 57421:
      return -12;
    case 57422:
      return -13;
    case 57423:
      return -14;
    case 57424:
      return -15;
    case 57425:
      return -11;
    case 57426:
      return -10;
    default:
      return codepoint;
  }
}

bool is_ascii_letter_or_symbol(int codepoint)
{
  if ((codepoint >= 'a' && codepoint <= 'z') || (codepoint >= 'A' && codepoint <= 'Z') || (codepoint >= '0' && codepoint <= '9'))
    return true;
  switch (codepoint)
  {
    case '`':
    case '-':
    case '=':
    case '[':
    case ']':
    case '\\':
    case ';':
    case '\'':
    case ',':
    case '.':
    case '/':
    case '!':
    case '@':
    case '#':
    case '$':
    case '%':
    case '^':
    case '&':
    case '*':
    case '(':
    case ')':
    case '_':
    case '+':
    case '|':
    case '~':
    case '{':
    case '}':
    case ':':
    case '<':
    case '>':
    case '?':
      return true;
    default:
      return false;
  }
}

int lowercase_ascii_letter(int codepoint)
{
  if (codepoint >= 'A' && codepoint <= 'Z')
    return codepoint + ('a' - 'A');
  return codepoint;
}

int kitty_key_identity_codepoint(KittyCsiUSequence const& parsed)
{
  auto const normalized = lowercase_ascii_letter(normalize_kitty_functional_codepoint(parsed.codepoint));
  if (is_ascii_letter_or_symbol(normalized))
    return normalized;
  if (parsed.base_layout_key)
    return lowercase_ascii_letter(normalize_kitty_functional_codepoint(*parsed.base_layout_key));
  return normalized;
}

int text_identity_codepoint(int codepoint)
{
  return lowercase_ascii_letter(normalize_kitty_functional_codepoint(codepoint));
}

Key control_key_from_codepoint(int codepoint)
{
  switch (codepoint)
  {
    case '0':
      return Key::Ctrl0;
    case '1':
      return Key::Ctrl1;
    case '2':
      return Key::Ctrl2;
    case '3':
      return Key::Ctrl3;
    case '4':
      return Key::Ctrl4;
    case '5':
      return Key::Ctrl5;
    case '6':
      return Key::Ctrl6;
    case '7':
      return Key::Ctrl7;
    case '8':
      return Key::Ctrl8;
    case '9':
      return Key::Ctrl9;
    case 'a':
      return Key::CtrlA;
    case 'b':
      return Key::CtrlB;
    case 'c':
      return Key::CtrlC;
    case 'd':
      return Key::CtrlD;
    case 'e':
      return Key::CtrlE;
    case 'f':
      return Key::CtrlF;
    case 'g':
      return Key::CtrlG;
    case 'h':
      return Key::CtrlH;
    case 'k':
      return Key::CtrlK;
    case 'l':
      return Key::CtrlL;
    case 'n':
      return Key::CtrlN;
    case 'o':
      return Key::CtrlO;
    case 'p':
      return Key::CtrlP;
    case 'r':
      return Key::CtrlR;
    case 's':
      return Key::CtrlS;
    case 't':
      return Key::CtrlT;
    case 'u':
      return Key::CtrlU;
    case 'v':
      return Key::CtrlV;
    case 'w':
      return Key::CtrlW;
    case 'x':
      return Key::CtrlX;
    case 'y':
      return Key::CtrlY;
    case 'z':
      return Key::CtrlZ;
    case ']':
      return Key::CtrlRightBracket;
    case '-':
      return Key::CtrlMinus;
    case '/':
      return Key::CtrlSlash;
    default:
      return Key::Unknown;
  }
}

Key alt_key_from_codepoint(int codepoint)
{
  switch (codepoint)
  {
    case 'b':
      return Key::AltB;
    case 'd':
      return Key::AltD;
    case 'f':
      return Key::AltF;
    case 'h':
      return Key::AltH;
    case 'j':
      return Key::AltJ;
    case 'k':
      return Key::AltK;
    case 'l':
      return Key::AltL;
    case 'w':
      return Key::AltW;
    case 'y':
      return Key::AltY;
    default:
      return Key::Unknown;
  }
}

Key key_from_codepoint_and_modifiers(int codepoint, int modifiers)
{
  auto const effective_modifiers = effective_kitty_modifiers(modifiers);
  if ((effective_modifiers & kKittyModifierSuper) != 0)
    return Key::Unknown;

  auto const normalized = normalize_kitty_functional_codepoint(codepoint);
  auto const has_shift = (effective_modifiers & kKittyModifierShift) != 0;
  auto const has_alt = (effective_modifiers & kKittyModifierAlt) != 0;
  auto const has_ctrl = (effective_modifiers & kKittyModifierCtrl) != 0;
  auto const identity = text_identity_codepoint(normalized);

  if (has_ctrl && has_shift && !has_alt && identity == 'p')
    return Key::CtrlShiftP;
  if (has_ctrl && has_alt && !has_shift && identity == ']')
    return Key::CtrlAltRightBracket;
  if (is_ascii_letter_or_symbol(identity))
  {
    if (has_ctrl && !has_alt && !has_shift)
      return control_key_from_codepoint(identity);
    if (has_alt && !has_ctrl && !has_shift)
      return alt_key_from_codepoint(identity);
  }

  switch (normalized)
  {
    case 13:
      if (has_shift && !has_ctrl && !has_alt)
        return Key::ShiftEnter;
      if (has_ctrl && !has_shift && !has_alt)
        return Key::CtrlEnter;
      if (has_alt && !has_shift && !has_ctrl)
        return Key::AltEnter;
      return effective_modifiers == 0 ? Key::Enter : Key::Unknown;
    case 9:
      if (has_shift && !has_ctrl && !has_alt)
        return Key::ShiftTab;
      return effective_modifiers == 0 ? Key::Tab : Key::Unknown;
    case 27:
      return effective_modifiers == 0 ? Key::Escape : Key::Unknown;
    case 32:
      if (has_ctrl && !has_shift && !has_alt)
        return Key::CtrlSpace;
      return effective_modifiers == 0 ? Key::Space : Key::Unknown;
    case 127:
      if (has_shift && !has_ctrl && !has_alt)
        return Key::ShiftBackspace;
      if (has_ctrl && !has_shift && !has_alt)
        return Key::CtrlBackspace;
      if (has_alt && !has_ctrl && !has_shift)
        return Key::AltBackspace;
      return effective_modifiers == 0 ? Key::Backspace : Key::Unknown;
    case -4:
    case -3:
    case -2:
    case -1:
      return cursor_key_from_direction_and_modifiers(normalized, effective_modifiers);
    case -10:
      if (has_shift && !has_ctrl && !has_alt)
        return Key::ShiftDelete;
      if (has_alt && !has_ctrl && !has_shift)
        return Key::AltDelete;
      return !has_alt ? Key::Delete : Key::Unknown;
    case -12:
      return Key::PageUp;
    case -13:
      return Key::PageDown;
    case -14:
      if (has_ctrl && has_shift && !has_alt)
        return Key::ShiftCtrlHome;
      if (has_ctrl && !has_shift && !has_alt)
        return Key::CtrlHome;
      if (has_shift && !has_ctrl && !has_alt)
        return Key::ShiftHome;
      return Key::Home;
    case -15:
      if (has_ctrl && has_shift && !has_alt)
        return Key::ShiftCtrlEnd;
      if (has_ctrl && !has_shift && !has_alt)
        return Key::CtrlEnd;
      if (has_shift && !has_ctrl && !has_alt)
        return Key::ShiftEnd;
      return Key::End;
    default:
      return Key::Unknown;
  }
}

Key key_from_kitty_csi_u_sequence(std::string_view sequence)
{
  auto const parsed = parse_kitty_csi_u_sequence(sequence);
  if (!parsed)
    return Key::Unknown;

  auto const modifiers = effective_kitty_modifiers(parsed->modifiers);
  if (parsed->event_type == 3)
    return Key::Unknown;
  auto const identity = kitty_key_identity_codepoint(*parsed);
  if (identity != text_identity_codepoint(parsed->codepoint))
    return key_from_codepoint_and_modifiers(identity, modifiers);
  return key_from_codepoint_and_modifiers(parsed->codepoint, modifiers);
}

void append_utf8_codepoint(std::string& text, int codepoint)
{
  if (codepoint < 0 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
    return;
  if (codepoint <= 0x7F)
  {
    text.push_back(static_cast<char>(codepoint));
  }
  else if (codepoint <= 0x7FF)
  {
    text.push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
    text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  else if (codepoint <= 0xFFFF)
  {
    text.push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
    text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
  else
  {
    text.push_back(static_cast<char>(0xF0 | ((codepoint >> 18) & 0x07)));
    text.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

std::optional<InputEvent> kitty_csi_u_printable_event(std::string_view sequence)
{
  auto const parsed = parse_kitty_csi_u_sequence(sequence);
  if (!parsed)
    return std::nullopt;

  auto const modifiers = effective_kitty_modifiers(parsed->modifiers);
  if (parsed->event_type == 3)
    return std::nullopt;
  if ((modifiers & ~(kKittyModifierShift | kKittyLockModifiers)) != 0)
    return std::nullopt;

  auto codepoint = parsed->codepoint;
  if ((modifiers & kKittyModifierShift) != 0 && parsed->shifted_key)
    codepoint = *parsed->shifted_key;
  codepoint = normalize_kitty_functional_codepoint(codepoint);
  if (codepoint < 32)
    return std::nullopt;

  std::string text;
  append_utf8_codepoint(text, codepoint);
  if (text.empty())
    return std::nullopt;
  return InputEvent{.key = Key::Character, .character = text.size() == 1 ? text.front() : '\0', .text = std::move(text), .mouse_column = 0, .mouse_row = 0};
}

std::optional<ModifyOtherKeysSequence> parse_modify_other_keys_sequence(std::string_view sequence)
{
  if (!sequence.starts_with('[') || sequence.size() < 8 || sequence.back() != '~')
    return std::nullopt;

  auto index = std::size_t{1};
  auto const escape_code = parse_unsigned_int(sequence, index);
  if (!escape_code || *escape_code != 27 || !consume_char(sequence, index, ';'))
    return std::nullopt;

  auto const modifier_value = parse_unsigned_int(sequence, index);
  if (!modifier_value || *modifier_value <= 0 || !consume_char(sequence, index, ';'))
    return std::nullopt;

  auto const codepoint = parse_unsigned_int(sequence, index);
  if (!codepoint || index + 1 != sequence.size() || sequence[index] != '~')
    return std::nullopt;

  return ModifyOtherKeysSequence{.codepoint = *codepoint, .modifiers = *modifier_value - 1};
}

Key key_from_modify_other_keys_sequence(std::string_view sequence)
{
  auto const parsed = parse_modify_other_keys_sequence(sequence);
  if (!parsed)
    return Key::Unknown;
  return key_from_codepoint_and_modifiers(parsed->codepoint, parsed->modifiers);
}

Key function_key_from_sequence(std::string_view sequence)
{
  if (sequence == "OP" || sequence == "[11~")
    return Key::F1;
  if (sequence == "OQ" || sequence == "[12~")
    return Key::F2;
  if (sequence == "OR" || sequence == "[13~")
    return Key::F3;
  if (sequence == "OS" || sequence == "[14~")
    return Key::F4;
  if (sequence == "[15~")
    return Key::F5;
  if (sequence == "[17~")
    return Key::F6;
  if (sequence == "[18~")
    return Key::F7;
  if (sequence == "[19~")
    return Key::F8;
  if (sequence == "[20~")
    return Key::F9;
  if (sequence == "[21~")
    return Key::F10;
  if (sequence == "[23~")
    return Key::F11;
  if (sequence == "[24~")
    return Key::F12;
  return Key::Unknown;
}

std::optional<InputEvent> modify_other_keys_printable_event(std::string_view sequence)
{
  auto const parsed = parse_modify_other_keys_sequence(sequence);
  if (!parsed)
    return std::nullopt;

  auto const modifiers = effective_kitty_modifiers(parsed->modifiers);
  if ((modifiers & ~(kKittyModifierShift | kKittyLockModifiers)) != 0)
    return std::nullopt;
  auto const codepoint = normalize_kitty_functional_codepoint(parsed->codepoint);
  if (codepoint < 32)
    return std::nullopt;

  std::string text;
  append_utf8_codepoint(text, codepoint);
  if (text.empty())
    return std::nullopt;
  return InputEvent{.key = Key::Character, .character = text.size() == 1 ? text.front() : '\0', .text = std::move(text), .mouse_column = 0, .mouse_row = 0};
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

  auto modifier = std::optional<int>{};
  while (index + 1 < sequence.size())
  {
    if (!consume_char(sequence, index, ';') && !consume_char(sequence, index, ':'))
      return Key::Unknown;
    auto const parsed_modifier = parse_unsigned_int(sequence, index);
    if (!parsed_modifier)
      return Key::Unknown;
    if (!modifier)
      modifier = parsed_modifier;
  }
  if (index + 1 != sequence.size())
    return Key::Unknown;

  auto effective_modifiers = 0;
  if (modifier)
  {
    if (*modifier <= 0)
      return Key::Unknown;
    effective_modifiers = effective_kitty_modifiers(*modifier - 1);
    if ((effective_modifiers & ~(kKittyModifierShift | kKittyModifierAlt | kKittyModifierCtrl)) != 0)
      return Key::Unknown;
  }

  if (*code == 1 || *code == 7)
    return home_end_key_from_modifiers(true, effective_modifiers);
  if (*code == 4 || *code == 8)
    return home_end_key_from_modifiers(false, effective_modifiers);
  if (*code == 3)
  {
    if (effective_modifiers == kKittyModifierShift)
      return Key::ShiftDelete;
    if (effective_modifiers == kKittyModifierAlt)
      return Key::AltDelete;
    return effective_modifiers == 0 ? Key::Delete : Key::Unknown;
  }
  if (*code == 2)
    return Key::Insert;
  if (*code == 5)
    return Key::PageUp;
  if (*code == 6)
    return Key::PageDown;
  return Key::Unknown;
}

std::optional<InputEvent> sgr_mouse_event(std::string_view sequence)
{
  if (!sequence.starts_with("[<") || sequence.size() < 7)
    return std::nullopt;
  auto index = std::size_t{2};
  auto const button = parse_unsigned_int(sequence, index);
  if (!button || !consume_char(sequence, index, ';'))
    return std::nullopt;
  auto const column = parse_unsigned_int(sequence, index);
  if (!column || !consume_char(sequence, index, ';'))
    return std::nullopt;
  auto const row = parse_unsigned_int(sequence, index);
  if (!row || index + 1 != sequence.size() || (sequence[index] != 'M' && sequence[index] != 'm'))
    return std::nullopt;
  auto key = Key::Unknown;
  auto const button_code = *button;
  auto const final = sequence[index];
  auto const is_motion = (button_code & 32) != 0;
  auto const is_wheel = (button_code & 64) != 0;
  auto const base_button = button_code & 3;
  if (is_wheel && final == 'M')
  {
    key = (button_code & 1) == 0 ? Key::MouseWheelUp : Key::MouseWheelDown;
  }
  else if (final == 'm' && base_button == 0 && !is_wheel)
  {
    key = Key::MouseLeftRelease;
  }
  else if (final == 'M' && is_motion && base_button == 0 && !is_wheel)
  {
    key = Key::MouseLeftDrag;
  }
  else if (final == 'M' && !is_motion && base_button == 0 && !is_wheel)
  {
    key = Key::MouseLeftClick;
  }
  return InputEvent{.key = key,
                    .character = '\0',
                    .text = {},
                    .mouse_column = key == Key::Unknown ? 0U : static_cast<std::size_t>(*column),
                    .mouse_row = key == Key::Unknown ? 0U : static_cast<std::size_t>(*row)};
}

Key sgr_mouse_key(std::string_view sequence)
{
  if (auto event = sgr_mouse_event(sequence))
    return event->key;
  return Key::Unknown;
}

bool is_legacy_mouse_sequence(std::string_view sequence)
{
  return sequence.starts_with("[M") && sequence.size() >= 5;
}

std::optional<InputEvent> legacy_mouse_event(std::string_view sequence)
{
  if (!is_legacy_mouse_sequence(sequence))
    return std::nullopt;
  auto const button_byte = static_cast<unsigned char>(sequence[2]);
  auto const column_byte = static_cast<unsigned char>(sequence[3]);
  auto const row_byte = static_cast<unsigned char>(sequence[4]);
  if (button_byte < 32 || column_byte < 32 || row_byte < 32)
    return key_event(Key::Unknown);
  auto const button = static_cast<unsigned int>(button_byte - 32U);
  auto key = Key::Unknown;
  auto const is_motion = (button & 32U) != 0;
  auto const is_wheel = (button & 64U) != 0;
  auto const base_button = button & 3U;
  if (is_wheel)
  {
    key = (button & 1U) == 0 ? Key::MouseWheelUp : Key::MouseWheelDown;
  }
  else if (base_button == 3U)
  {
    key = Key::MouseLeftRelease;
  }
  else if (is_motion && base_button == 0U)
  {
    key = Key::MouseLeftDrag;
  }
  else if (!is_motion && base_button == 0U)
  {
    key = Key::MouseLeftClick;
  }
  return InputEvent{.key = key,
                    .character = '\0',
                    .text = {},
                    .mouse_column = key == Key::Unknown ? 0U : static_cast<std::size_t>(column_byte - 32U),
                    .mouse_row = key == Key::Unknown ? 0U : static_cast<std::size_t>(row_byte - 32U)};
}

Key legacy_mouse_key(std::string_view sequence)
{
  if (auto event = legacy_mouse_event(sequence))
    return event->key;
  return Key::Unknown;
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
  reset_keyboard_protocol_negotiation();
  push_kitty_keyboard_protocol();
  restore_signal_mask();
  return session;
}

bool detail::force_terminal_cursor_visible() noexcept
{
  static_cast<void>(curs_set(0));
  return curs_set(1) != ERR;
}

void CursesSession::restore() noexcept
{
  if (!active_)
    return;
  static_cast<void>(set_term(static_cast<SCREEN*>(screen_.get())));
  disable_modify_other_keys_fallback();
  pop_kitty_keyboard_protocol();
  set_bracketed_paste(false);
  if (restore_terminal_attrs_)
  {
    static_cast<void>(tcsetattr(STDIN_FILENO, TCSANOW, &previous_terminal_attrs_));
    restore_terminal_attrs_ = false;
  }
  static_cast<void>(detail::force_terminal_cursor_visible());
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

std::string_view terminal_kitty_keyboard_push_sequence()
{
  return kKittyKeyboardPushSequence;
}

std::string_view terminal_kitty_keyboard_query_sequence()
{
  return kKittyKeyboardQuerySequence;
}

std::string_view terminal_kitty_keyboard_pop_sequence()
{
  return kKittyKeyboardPopSequence;
}

std::string_view terminal_modify_other_keys_enable_sequence()
{
  return kModifyOtherKeysEnableSequence;
}

std::string_view terminal_modify_other_keys_disable_sequence()
{
  return kModifyOtherKeysDisableSequence;
}

std::optional<int> terminal_kitty_keyboard_flags_response(std::string_view sequence)
{
  if (!sequence.starts_with("[?") || sequence.size() < 4 || sequence.back() != 'u')
    return std::nullopt;

  auto index = std::size_t{2};
  auto const flags = parse_unsigned_int(sequence, index);
  if (!flags || index + 1 != sequence.size() || sequence[index] != 'u')
    return std::nullopt;
  return flags;
}

bool terminal_device_attributes_response(std::string_view sequence)
{
  if (!sequence.starts_with("[?") || sequence.size() < 4 || sequence.back() != 'c')
    return false;

  auto index = std::size_t{2};
  if (!parse_unsigned_int(sequence, index))
    return false;
  while (index + 1 < sequence.size())
  {
    if (!consume_char(sequence, index, ';'))
      return false;
    if (!parse_unsigned_int(sequence, index))
      return false;
  }
  return index + 1 == sequence.size() && sequence[index] == 'c';
}

KeyboardProtocolResponseAction terminal_keyboard_protocol_response_action(std::string_view sequence, bool kitty_response_seen, bool modify_other_keys_enabled)
{
  if (auto const flags = terminal_kitty_keyboard_flags_response(sequence))
  {
    if (*flags > 0)
      return modify_other_keys_enabled ? KeyboardProtocolResponseAction::DisableModifyOtherKeys : KeyboardProtocolResponseAction::None;
    return modify_other_keys_enabled ? KeyboardProtocolResponseAction::None : KeyboardProtocolResponseAction::EnableModifyOtherKeys;
  }

  if (terminal_device_attributes_response(sequence))
  {
    if (!kitty_response_seen && !modify_other_keys_enabled)
      return KeyboardProtocolResponseAction::EnableModifyOtherKeys;
  }

  return KeyboardProtocolResponseAction::None;
}

bool terminal_keyboard_protocol_handle_response(std::string_view sequence)
{
  if (terminal_kitty_keyboard_flags_response(sequence))
  {
    auto const action = terminal_keyboard_protocol_response_action(sequence, g_keyboard_protocol_kitty_response_seen, g_modify_other_keys_enabled);
    g_keyboard_protocol_kitty_response_seen = true;
    apply_keyboard_protocol_response_action(action);
    return true;
  }

  if (terminal_device_attributes_response(sequence))
  {
    apply_keyboard_protocol_response_action(
        terminal_keyboard_protocol_response_action(sequence, g_keyboard_protocol_kitty_response_seen, g_modify_other_keys_enabled));
    return true;
  }

  return false;
}

InputEvent terminal_escape_sequence_event(std::string_view sequence)
{
  if (auto event = sgr_mouse_event(sequence))
    return *event;
  if (auto event = legacy_mouse_event(sequence))
    return *event;
  auto const key = terminal_escape_sequence_key(sequence);
  if (key != Key::Unknown)
    return key_event(key);
  if (auto event = kitty_csi_u_printable_event(sequence))
    return *event;
  if (auto event = modify_other_keys_printable_event(sequence))
    return *event;
  return key_event(Key::Unknown);
}

Key terminal_escape_sequence_key(std::string_view sequence)
{
  if (sequence == "\x7f" || sequence == "\b")
    return Key::AltBackspace;
  if (sequence == "\x1d")
    return Key::CtrlAltRightBracket;
  if (sequence == "\x1f" || is_ctrl_minus_csi_u(sequence))
    return Key::CtrlMinus;
  if (sequence == std::string_view("\x00", 1))
    return Key::CtrlSpace;
  if (sequence == "\r" || sequence == "\n")
    return Key::AltEnter;
  if (sequence == "[Z")
    return Key::ShiftTab;
  if (sequence == "[1;6P" || is_ctrl_shift_p_csi_u(sequence))
    return Key::CtrlShiftP;
  if (sequence == "[3$")
    return Key::ShiftDelete;
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
  if (auto const key = csi_cursor_key(sequence); key != Key::Unknown)
    return key;
  if (sequence == "[d")
    return Key::ShiftArrowLeft;
  if (sequence == "[c")
    return Key::ShiftArrowRight;
  if (sequence == "[a")
    return Key::ShiftArrowUp;
  if (sequence == "[b")
    return Key::ShiftArrowDown;
  if (auto const key = csi_home_end_key(sequence); key != Key::Unknown)
    return key;
  if (sequence == "[7$")
    return Key::ShiftHome;
  if (sequence == "[8$")
    return Key::ShiftEnd;
  if (is_legacy_shift_enter_sequence(sequence) || is_shift_enter_csi_u(sequence))
    return Key::ShiftEnter;
  if (is_legacy_ctrl_enter_sequence(sequence) || is_ctrl_enter_csi_u(sequence))
    return Key::CtrlEnter;
  if (is_legacy_alt_enter_sequence(sequence) || is_alt_enter_csi_u(sequence))
    return Key::AltEnter;
  if (auto const key = function_key_from_sequence(sequence); key != Key::Unknown)
    return key;
  if (auto const key = key_from_kitty_csi_u_sequence(sequence); key != Key::Unknown)
    return key;
  if (auto const key = key_from_modify_other_keys_sequence(sequence); key != Key::Unknown)
    return key;
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
    if (sequence == "[3$" || sequence == "[7$" || sequence == "[8$")
      return true;
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
    if (byte == 0x00U || byte == 0x08U || byte == 0x0AU || byte == 0x0DU || byte == 0x1DU || byte == 0x1FU || byte == 0x7FU)
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
