#pragma once

#include "LayoutItem.h"

namespace ava::tui::terminal {

class Spacer : public LayoutItem
{
 private:
  // Construct a Spacer.
  Spacer(LayoutItem&& layout_item) : LayoutItem(std::move(layout_item)) { }

 public:
  static std::unique_ptr<Spacer> create(LayoutItem::Properties layout_properties = {.minimum_width = 1})
  {
    return std::unique_ptr<Spacer>(new Spacer(layout_properties));
  }

  AVA_DEBUG_PRINT_MEMBERS_ON_BASE(LayoutItem)
};

} // namespace ava::tui::terminal
