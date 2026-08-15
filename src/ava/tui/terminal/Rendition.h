#pragma once

#include "Attributes.h"
#include "ColorPair.h"
#include "ava/debug/print_members_on.h"

namespace ava::tui::terminal {

class Window;

// class Rendition
//
// A ColorPair / Attributes pair.
//
// Can be used along side UTF8 text to fully define how a string is displayed on the terminal.
//
class Rendition
{
 private:
  ColorPair color_pair_;
  Attributes attributes_;

 public:
  // Construct a Rendition with `color_pair` and `attributes`.
  Rendition(ColorPair color_pair = {}, Attributes attributes = {}) : color_pair_(color_pair), attributes_(attributes) { }

  // Accessors

  ColorPair color_pair() const { return color_pair_; }
  ColorPair& color_pair() { return color_pair_; }

  Attributes attributes() const { return attributes_; }
  Attributes& attributes() { return attributes_; }

  // Equal when both use the same color pair and the same attributes.
  friend bool operator==(Rendition const& lhs, Rendition const& rhs)
  {
    return lhs.color_pair_.index() == rhs.color_pair_.index() && lhs.attributes_.mask() == rhs.attributes_.mask();
  }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
