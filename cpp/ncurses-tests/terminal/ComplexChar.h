#pragma once

#include "Rendition.h"
#include <cstdint>

namespace terminal {

// ComplexChar
//
// Combines a (potentially empty) (variable width) character with Attributes and a color pair (by index).
class ComplexChar
{
 private:
  static constexpr int CCHARW_MAX = 5;                  // The same value that ncursesw uses.
  static constexpr wchar_t const* space_ = L" ";

  wchar_t cell_character_[CCHARW_MAX];                  // Grapheme cluster, to be displayed in a single terminal cell.
  Rendition rendition_;                                 // Text fore- and background colors and attributes that apply.

 public:
  ComplexChar(wchar_t const* cell_character, Rendition rendition);

  // Specify ColorPair and (optionally) Attributes.
  ComplexChar(wchar_t const* cell_character, ColorPair color_pair, Attributes attributes = {}) :
    ComplexChar(cell_character, Rendition{color_pair, attributes}) { }

  // Use default colors.
  ComplexChar(wchar_t const* cell_character, Attributes attributes = {}) :
    ComplexChar(cell_character, 0, attributes) { }

  // Empty variable width character.
  ComplexChar(Rendition rendition)                              : ComplexChar(space_, rendition) { }
  ComplexChar(ColorPair color_pair, Attributes attributes = {}) : ComplexChar(space_, color_pair, attributes) { }
  ComplexChar(Attributes attributes = {})                       : ComplexChar(space_, attributes) { }

  // Accessors.
  wchar_t const* cell_character() const { return cell_character_; }
  wchar_t* cell_character() { return cell_character_; }

  Rendition rendition() const { return rendition_; }
  Rendition& rendition() { return rendition_; }

  // Manipulator.
  void set_rendition(Rendition rendition) { rendition_ = rendition; }
};

} // namespace terminal
