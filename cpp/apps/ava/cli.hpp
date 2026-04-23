#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace ava::app {

struct CliOptions {
  std::optional<std::string> goal;
  std::optional<std::string> provider;
  std::optional<std::string> model;
  bool resume{false};
  std::optional<std::string> session_id;
  bool json{false};
  std::size_t max_turns{16};
  bool max_turns_explicit{false};
  bool auto_approve{false};
  bool show_version{false};
  bool smoke_mode{false};
};

[[nodiscard]] CliOptions parse_cli_or_throw(int argc, char** argv);

}  // namespace ava::app
