#pragma once

#include "BasicWindow.h"
#include "debug.h"

namespace ava::tui::terminal {

using InnerWindow = BasicWindow;        // The writable area inside the margin.

class Window : public InnerWindow
{
 private:
  BasicWindow outer_window_;            // The handle to the outer window. If we have no border then this wraps a null-pointer.
  Border border_;                       // The border that is drawn around the inner window.

 public:
  // Construct a Window with an optional border.
  Window(Dimension size, Position pos, Border const& border = {}) : outer_window_(size, pos), border_(border)
  {
    if (has_margin())
    {
      InnerWindow inner_window(outer_window_.derwin(border.margin()));
      std::swap(InnerWindow::impl_, inner_window.impl_);                // Use inner_window for the base class.
      draw_border();
    }
    else
      std::swap(InnerWindow::impl_, outer_window_.impl_);               // Make the base class the outer window and set outer_window_ to null.
  }

  // Accessors.

  BasicWindow const& outer_window() const { return outer_window_.impl_ ? outer_window_ : static_cast<InnerWindow const&>(*this); }
  BasicWindow& outer_window() { return outer_window_.impl_ ? outer_window_ : static_cast<InnerWindow&>(*this); }

  Border const& border() const { return border_; }

  // Return true if this Window has a margin (and border).
  bool has_margin() const { return !border_.empty(); }

  // (Re)draw the border of the outer window.
  void draw_border()
  {
    // Do not call this function on a window with an empty margin.
    ASSERT(outer_window_.impl_);
    outer_window_.set_border(border_);
  }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
