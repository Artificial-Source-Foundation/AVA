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
  HorizontalLayout() = default;

  void append(std::unique_ptr<LayoutItem>&& layout_item)
  {
    layout_items_.push_back(std::move(layout_item));
  }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
