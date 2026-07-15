#pragma once

#include "ava/debug/print_members_on.h"

#include <string>
#include <vector>

namespace ava::agent {

enum class ToolVisibilityMode
{
  Default,
  NoBuiltinTools,
  NoTools,
};

struct ToolVisibilityOptions
{
  ToolVisibilityMode mode = ToolVisibilityMode::Default;
  std::vector<std::string> included_tools = {};
  std::vector<std::string> excluded_tools = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] std::string to_string(ToolVisibilityMode mode);

}  // namespace ava::agent
