#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace ava::tui {

struct TuiOptions {
  std::optional<std::string> provider;
  std::optional<std::string> model;
  bool resume{false};
  std::optional<std::string> session_id;
  std::size_t max_turns{16};
  bool max_turns_explicit{false};
  bool auto_approve{false};
};

TuiOptions parse_tui_options_or_throw(int argc, char** argv);

}  // namespace ava::tui
