#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ava/permissions/permission.h"

namespace ava::permissions::detail {

struct ParsedCommand {
  bool ok = false;
  std::string reason;
  std::vector<std::string> argv;
};

[[nodiscard]] ParsedCommand parse_command_argv(std::string_view command);
[[nodiscard]] bool is_safe_relative_path_arg(std::string_view value);

}  // namespace ava::permissions::detail
