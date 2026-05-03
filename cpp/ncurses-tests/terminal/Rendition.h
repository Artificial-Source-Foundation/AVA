#pragma once

#include "ColorPair.h"
#include "Attributes.h"

namespace terminal {

// class Rendition
//
// A ColorPair / Attributes pair.
//
// Can be used along side UTF8 text to full define how a string is displayed on the terminal.
//
class Rendition
{
 private:
  ColorPair color_pair_;
  Attributes attributes_;

 public:
  Rendition(ColorPair color_pair, Attributes attributes = {}) : color_pair_(color_pair), attributes_(attributes) { }

  // Accessors

  ColorPair color_pair() const { return color_pair_; }
  ColorPair& color_pair() { return color_pair_; }

  Attributes attributes() const { return attributes_; }
  Attributes& attributes() { return attributes_; }
};

} // namespace terminal
