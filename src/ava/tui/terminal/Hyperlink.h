#pragma once

#include "ava/debug/print_members_on.h"
#include <string>

namespace ava::tui::terminal {

struct Hyperlink
{
  std::string link_;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
