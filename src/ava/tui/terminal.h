#pragma once

#include "ava/core/result.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <termios.h>

namespace ava::tui {

enum class Key
{
  Character,
  Enter,
  Backspace,
  Delete,
  Tab,
  Space,
  ShiftTab,
  Escape,
  ArrowUp,
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  CtrlArrowLeft,
  CtrlArrowRight,
  AltArrowUp,
  AltArrowLeft,
  AltArrowRight,
  PageUp,
  PageDown,
  Home,
  End,
  MouseWheelUp,
  MouseWheelDown,
  MouseLeftClick,
  ShiftEnter,
  CtrlEnter,
  AltEnter,
  CtrlA,
  CtrlB,
  CtrlC,
  CtrlD,
  CtrlE,
  CtrlF,
  CtrlH,
  CtrlK,
  CtrlL,
  CtrlMinus,
  CtrlN,
  CtrlO,
  CtrlP,
  CtrlShiftP,
  CtrlR,
  CtrlRightBracket,
  CtrlS,
  CtrlT,
  CtrlU,
  CtrlW,
  CtrlY,
  CtrlZ,
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
[[nodiscard]] Key terminal_escape_sequence_key(std::string_view sequence);
[[nodiscard]] bool terminal_escape_sequence_complete(std::string_view sequence);
[[nodiscard]] bool terminal_escape_sequence_should_discard(std::string_view sequence);
[[nodiscard]] bool terminal_is_tty();
[[nodiscard]] bool terminal_signal_received();
[[nodiscard]] int terminal_signal_number();
void clear_terminal_signal();

}  // namespace ava::tui
