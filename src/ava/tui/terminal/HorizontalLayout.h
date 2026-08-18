#pragma once

#include "LayoutItem.h"

namespace ava::tui::terminal {

// class HorizontalLayout
//
// A container for adjacent LayoutItem's.
//
class HorizontalLayout
{
 private:
  std::vector<std::unique_ptr<LayoutItem>> layout_items_;

 public:
  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
