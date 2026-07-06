#include "sys.h"
#include "tool_visibility.h"

#ifdef CWDEBUG
#include "ava/debug/debug_ostream_operators.h"
#endif

namespace ava::agent {

#ifdef CWDEBUG
// clang-format off

void ToolVisibilityOptions::print_members(std::ostream& os, char const* prefix) const
{
  LIBCWD_USING_OSTREAM_PRELUDE
  os << prefix
    << "mode:" << to_string(mode)
    << ", included_tools:" << included_tools
    << ", excluded_tools:" << excluded_tools;
}

// clang-format on
#endif // CWDEBUG

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

