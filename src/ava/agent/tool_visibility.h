#pragma once

#include <string>
#include <vector>
#include "debug.h"

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
};

[[nodiscard]] std::string to_string(ToolVisibilityMode mode);

}  // namespace ava::agent
