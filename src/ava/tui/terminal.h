#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "ava/core/result.h"

namespace ava::tui {

enum class Key {
  Character,
  Enter,
  Backspace,
  Tab,
  Escape,
  ArrowUp,
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  PageUp,
  PageDown,
  MouseWheelUp,
  MouseWheelDown,
  MouseLeftClick,
  ShiftEnter,
  CtrlA,
  CtrlB,
  CtrlC,
  CtrlD,
  CtrlE,
  CtrlF,
  CtrlK,
  CtrlT,
  CtrlU,
  CtrlW,
  CtrlY,
  CtrlZ,
  Unknown
};

struct InputEvent {
  Key key = Key::Unknown;
  char character = '\0';
  std::string text = {};
  std::size_t mouse_column = 0;
  std::size_t mouse_row = 0;
};

class CursesSession {
 public:
  CursesSession(const CursesSession&) = delete;
  CursesSession& operator=(const CursesSession&) = delete;
  CursesSession(CursesSession&& other) noexcept;
  CursesSession& operator=(CursesSession&& other) noexcept;
  ~CursesSession();

  [[nodiscard]] static ava::core::Result<CursesSession> enter();

 private:
  struct ScreenDeleter {
    void operator()(void* screen) const noexcept;
  };

  explicit CursesSession(void* screen);

  void restore() noexcept;

  std::unique_ptr<void, ScreenDeleter> screen_;
  std::string previous_locale_;
  bool active_ = false;
};

void erase_last_utf8_codepoint(std::string& text);
[[nodiscard]] bool terminal_is_tty();
[[nodiscard]] bool terminal_signal_received();
void clear_terminal_signal();

}  // namespace ava::tui
