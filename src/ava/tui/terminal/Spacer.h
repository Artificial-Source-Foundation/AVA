#pragma once

#include "LayoutItem.h"

namespace ava::tui::terminal {

class Spacer final : public LayoutItem
{
 private:
  // Construct a Spacer.
  Spacer(LayoutItem::Properties const& layout_properties) : LayoutItem(layout_properties)
  {
    // Use an unbounded natural width, making it greedy. Such an item must have shrink priority zero.
    initialize_cached_natural_width(greedy);
    // Use a priority of 0 for a Spacer.
    ASSERT(shrink_priority() == 0);
  }

 public:
  static std::unique_ptr<Spacer> create(LayoutItem::Properties layout_properties = {.minimum_width = 1})
  {
    return std::unique_ptr<Spacer>(new Spacer(layout_properties));
  }

  AVA_DEBUG_PRINT_MEMBERS_ON_BASE(LayoutItem)
};

} // namespace ava::tui::terminal
