#pragma once

#include "ava/tui/terminal.h"

#include <chrono>
#include <optional>
#include <string>
#include "debug.h"

namespace ava::tui::runtime_input {

struct RuntimeInput
{
  InputEvent event;
  std::string text;
  bool bracketed_paste = false;
  bool resize = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] std::optional<std::string> printable_jump_target(RuntimeInput const& input);
[[nodiscard]] RuntimeInput read_curses_input();
[[nodiscard]] std::optional<RuntimeInput> poll_curses_input();
[[nodiscard]] std::optional<RuntimeInput> read_curses_input_with_timeout(std::chrono::milliseconds timeout);

}  // namespace ava::tui::runtime_input
