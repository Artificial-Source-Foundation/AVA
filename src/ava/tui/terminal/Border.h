#pragma once

#include "Margin.h"
#include "Rendition.h"
#include "Box.h"

namespace ava::tui::terminal {

// class Border
//
// Data required to draw a border.
//
class Border
{
 private:
  Margin margin_;               // The margin to be used for this border; a size of 0 means that there is no border on that side!
  Rendition rendition_;         // The rendition to use for this border.
  Box box_characters_;          // The eight characters used for the border.

 public:
  // Construct a non-existent Border.
  Border() : margin_{}, rendition_{ColorPair{}}, box_characters_{} { }

  // Construct a Border using `margin`, `rendition` and `box_characters`.
  Border(Margin margin,  Rendition rendition, Box const& box_characters = {Box::default_box}) : margin_(margin), rendition_(rendition), box_characters_(box_characters) { }

  // Accessors.
  Margin const& margin() const { return margin_; }
  Rendition const& rendition() const { return rendition_; }
  Box const& box_characters() const { return box_characters_; }

  // Convenience accessor.
  bool empty() const { return margin_.empty(); }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
