#pragma once

#include "LayoutItem.h"
#include <vector>

namespace ava::tui::terminal {

class BasicWindow;
class Rendition;

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

  void set_width(uint32_t columns);

  // Write this HorizontalLayout to a BasicWindow at the current cursor position.
  void write_to(BasicWindow& basic_window, Rendition const& default_rendition) const;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
