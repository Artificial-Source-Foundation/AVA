#pragma once

#include "ava/core/result.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <termios.h>

namespace ava::tui {

enum class Key
{
  Character,
  Enter,
  Backspace,
  ShiftBackspace,
  CtrlBackspace,
  Delete,
  ShiftDelete,
  Insert,
  Clear,
  Tab,
  Space,
  CtrlSpace,
  Ctrl0,
  Ctrl1,
  Ctrl2,
  Ctrl3,
  Ctrl4,
  Ctrl5,
  Ctrl6,
  Ctrl7,
  Ctrl8,
  Ctrl9,
  ShiftTab,
  ShiftL,
  ShiftT,
  Escape,
  ArrowUp,
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  ShiftArrowUp,
  ShiftArrowDown,
  ShiftArrowLeft,
  ShiftArrowRight,
  ShiftCtrlArrowLeft,
  ShiftCtrlArrowRight,
  ShiftAltArrowLeft,
  ShiftAltArrowRight,
  CtrlArrowLeft,
  CtrlArrowRight,
  AltArrowUp,
  AltArrowDown,
  AltArrowLeft,
  AltArrowRight,
  PageUp,
  PageDown,
  Home,
  End,
  CtrlHome,
  CtrlEnd,
  ShiftHome,
  ShiftEnd,
  ShiftCtrlHome,
  ShiftCtrlEnd,
  MouseWheelUp,
  MouseWheelDown,
  MouseLeftClick,
  MouseLeftDrag,
  MouseLeftRelease,
  ShiftEnter,
  CtrlEnter,
  AltEnter,
  CtrlA,
  CtrlB,
  CtrlC,
  CtrlD,
  CtrlE,
  CtrlF,
  CtrlG,
  CtrlH,
  CtrlK,
  CtrlL,
  CtrlMinus,
  CtrlSlash,
  CtrlN,
  CtrlO,
  CtrlP,
  CtrlShiftP,
  CtrlR,
  CtrlRightBracket,
  CtrlS,
  CtrlT,
  CtrlU,
  CtrlV,
  CtrlW,
  CtrlX,
  CtrlY,
  CtrlZ,
  F1,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,
  AltBackspace,
  AltB,
  AltD,
  AltDelete,
  AltF,
  AltH,
  AltJ,
  AltK,
  AltL,
  AltW,
  CtrlAltRightBracket,
  AltY,
  Unknown
};

struct InputEvent
{
  Key key = Key::Unknown;
  char character = '\0';
  std::string text = {};
  std::size_t mouse_column = 0;
  std::size_t mouse_row = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

enum class KeyboardProtocolResponseAction
{
  None,
  EnableModifyOtherKeys,
  DisableModifyOtherKeys
};

class CursesSession
{
 public:
  CursesSession(CursesSession const&) = delete;
  CursesSession& operator=(CursesSession const&) = delete;
  CursesSession(CursesSession&& other) noexcept;
  CursesSession& operator=(CursesSession&& other) noexcept;
  ~CursesSession();

  [[nodiscard]] static ava::core::Result<CursesSession> enter();

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  struct ScreenDeleter
  {
    void operator()(void* screen) const noexcept;
  };

  explicit CursesSession(void* screen);

  void restore() noexcept;

  std::unique_ptr<void, ScreenDeleter> screen_;
  std::string previous_locale_;
  termios previous_terminal_attrs_{};
  bool restore_terminal_attrs_ = false;
  bool active_ = false;
};

void erase_last_utf8_codepoint(std::string& text);
[[nodiscard]] int terminal_escape_delay_ms();
[[nodiscard]] std::string_view terminal_kitty_keyboard_push_sequence();
[[nodiscard]] std::string_view terminal_kitty_keyboard_query_sequence();
[[nodiscard]] std::string_view terminal_kitty_keyboard_pop_sequence();
[[nodiscard]] std::string_view terminal_modify_other_keys_enable_sequence();
[[nodiscard]] std::string_view terminal_modify_other_keys_disable_sequence();
[[nodiscard]] std::optional<int> terminal_kitty_keyboard_flags_response(std::string_view sequence);
[[nodiscard]] bool terminal_device_attributes_response(std::string_view sequence);
[[nodiscard]] KeyboardProtocolResponseAction terminal_keyboard_protocol_response_action(
    std::string_view sequence, bool kitty_response_seen, bool modify_other_keys_enabled);
[[nodiscard]] bool terminal_keyboard_protocol_handle_response(std::string_view sequence);
[[nodiscard]] InputEvent terminal_escape_sequence_event(std::string_view sequence);
[[nodiscard]] Key terminal_escape_sequence_key(std::string_view sequence);
[[nodiscard]] bool terminal_escape_sequence_complete(std::string_view sequence);
[[nodiscard]] bool terminal_escape_sequence_should_discard(std::string_view sequence);
[[nodiscard]] bool terminal_is_tty();
[[nodiscard]] bool terminal_signal_received();
[[nodiscard]] int terminal_signal_number();
void clear_terminal_signal();

}  // namespace ava::tui
