#include "sys.h"
#include "tool_visibility.h"
#include "debug.h"

namespace ava::agent {

std::string to_string(ToolVisibilityMode mode)
{
  switch (mode)
  {
    case ToolVisibilityMode::Default:
      return "Default";
    case ToolVisibilityMode::NoBuiltinTools:
      return "NoBuiltinTools";
    case ToolVisibilityMode::NoTools:
      return "NoTools";
  }
  AI_NEVER_REACHED
}

} // ava::agent

