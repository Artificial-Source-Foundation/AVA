#pragma once

#include "LayoutItem.h"

namespace ava::tui::terminal {

class Spacer : public LayoutItem
{
 public:
  // Construct a Spacer.
  Spacer(LayoutItem::Properties layout_properties = {.minimum_width = greedy}) : LayoutItem(layout_properties) { }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
