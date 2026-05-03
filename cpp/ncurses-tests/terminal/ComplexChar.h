#pragma once

#include "Rendition.h"
#include "GraphemeCluster.h"
#include <cstdint>

namespace terminal {

// ComplexChar
//
// Combines a (potentially empty) (variable width) character with Attributes and a color pair (by index).
class ComplexChar
{
 private:
  GraphemeCluster cell_character_;      // Grapheme cluster, to be displayed in a single terminal cell.
  Rendition rendition_;                 // Color pair and attributes that apply.

 public:
  // Construct a ComplexChar from a grapheme and a rendition.
  ComplexChar(GraphemeCluster const& cell_character, Rendition rendition);

  // Specify ColorPair and (optionally) Attributes.
  ComplexChar(GraphemeCluster const& cell_character, ColorPair color_pair, Attributes attributes = {}) :
    ComplexChar(cell_character, Rendition{color_pair, attributes}) { }

  // Use default colors.
  ComplexChar(GraphemeCluster const& cell_character, Attributes attributes = {}) :
    ComplexChar(cell_character, 0, attributes) { }

  // Empty variable width character.
  ComplexChar(Rendition rendition)                              : ComplexChar(GraphemeCluster{}, rendition) { }
  ComplexChar(ColorPair color_pair, Attributes attributes = {}) : ComplexChar(GraphemeCluster{}, color_pair, attributes) { }
  ComplexChar(Attributes attributes = {})                       : ComplexChar(GraphemeCluster{}, attributes) { }

  // Accessors.
  GraphemeCluster const& cell_character() const { return cell_character_; }
  GraphemeCluster& cell_character() { return cell_character_; }

  Rendition rendition() const { return rendition_; }
  Rendition& rendition() { return rendition_; }

  // Manipulator.
  void set_rendition(Rendition rendition) { rendition_ = rendition; }
};

} // namespace terminal
