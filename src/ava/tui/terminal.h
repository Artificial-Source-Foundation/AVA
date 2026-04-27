#pragma once

#include <termios.h>

#include <string>

#include "ava/core/result.h"

namespace ava::tui {

enum class Key { Character, Enter, Backspace, Tab, Escape, CtrlC, CtrlD, Unknown };

struct InputEvent {
  Key key = Key::Unknown;
  char character = '\0';
};

class RawTerminalGuard {
 public:
  RawTerminalGuard(const RawTerminalGuard&) = delete;
  RawTerminalGuard& operator=(const RawTerminalGuard&) = delete;
  RawTerminalGuard(RawTerminalGuard&& other) noexcept;
  RawTerminalGuard& operator=(RawTerminalGuard&& other) noexcept;
  ~RawTerminalGuard();

  [[nodiscard]] static ava::core::Result<RawTerminalGuard> enable(int input_fd);

 private:
  RawTerminalGuard(int input_fd, termios original);

  int input_fd_ = -1;
  termios original_{};
  bool active_ = false;
};

[[nodiscard]] InputEvent parse_input_byte(unsigned char byte);
void erase_last_utf8_codepoint(std::string& text);
[[nodiscard]] bool terminal_is_tty();
[[nodiscard]] ava::core::VoidResult write_terminal(std::string_view text);

}  // namespace ava::tui
